#include "esp32s3_storage.h"
#include "esp32s3_norcow.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "common.h"
#include "flash_area.h"
#include "norcow_config.h"
#include "rand.h"
#include "port_error.h"
#include "sha2.h"

#define STORAGE_LABEL "trezor_storage"
#define STORAGE_SIZE 0x10000u
#define PAGE_SIZE 0x1000u
#define PAGE_COUNT 16u
#define SALT_PAGE_A 0u
#define SALT_PAGE_B 15u
#define SALT_VERSION 1u
#define SALT_LENGTH 32u
#define SALT_COMMIT 0x00000000u

static const char *TAG = "trezor-storage";
static const uint8_t SALT_MAGIC[8] = {'T', 'R', 'Z', 'R', 'S', 'A', 'L', 'T'};

typedef struct __attribute__((packed, aligned(4))) {
  uint8_t magic[8];
  uint16_t version;
  uint16_t length;
  uint8_t salt[SALT_LENGTH];
  uint8_t digest[SHA256_DIGEST_LENGTH];
  uint32_t commit;
} salt_record_t;

_Static_assert(sizeof(salt_record_t) == 80, "salt record layout changed");

static const esp_partition_t *storage_partition;
static const uint8_t *storage_map;
static esp_partition_mmap_handle_t storage_map_handle;
static SemaphoreHandle_t flash_mutex;
static uint32_t unlock_depth;
static uint8_t persistent_salt[SALT_LENGTH];
static bool salt_loaded;

const flash_area_t STORAGE_AREAS[NORCOW_SECTOR_COUNT] = {
    {.subarea = {{.first_sector = 1, .num_sectors = 7}}, .num_subareas = 1},
    {.subarea = {{.first_sector = 8, .num_sectors = 7}}, .num_subareas = 1},
};

static bool bounds_ok(uint32_t offset, size_t size) {
  return offset <= STORAGE_SIZE && size <= STORAGE_SIZE - offset;
}

static bool is_erased(const uint8_t *p, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (p[i] != 0xff) return false;
  }
  return true;
}

static bool direct_write(uint32_t offset, const void *data, size_t size) {
  if (!bounds_ok(offset, size) || data == NULL || (offset & 3u) != 0 ||
      (size & 3u) != 0) {
    return false;
  }
  const uint8_t *src = data;
  for (size_t i = 0; i < size; i++) {
    if ((storage_map[offset + i] & src[i]) != src[i]) return false;
  }
  if (esp_partition_write(storage_partition, offset, data, size) != ESP_OK) {
    return false;
  }
  return memcmp(storage_map + offset, data, size) == 0;
}

static bool direct_erase_page(uint32_t page) {
  if (page >= PAGE_COUNT) return false;
  const uint32_t offset = page * PAGE_SIZE;
  if (esp_partition_erase_range(storage_partition, offset, PAGE_SIZE) != ESP_OK) {
    return false;
  }
  return is_erased(storage_map + offset, PAGE_SIZE);
}

static void salt_digest(const salt_record_t *record, uint8_t digest[32]) {
  sha256_Raw((const uint8_t *)record, offsetof(salt_record_t, digest), digest);
}

static bool salt_valid(uint32_t page, salt_record_t *out) {
  salt_record_t record;
  memcpy(&record, storage_map + page * PAGE_SIZE, sizeof(record));
  uint8_t digest[32];
  salt_digest(&record, digest);
  const bool valid = memcmp(record.magic, SALT_MAGIC, sizeof(SALT_MAGIC)) == 0 &&
                     record.version == SALT_VERSION &&
                     record.length == SALT_LENGTH &&
                     record.commit == SALT_COMMIT &&
                     memcmp(record.digest, digest, sizeof(digest)) == 0;
  memset(digest, 0, sizeof(digest));
  if (valid && out != NULL) *out = record;
  memset(&record, 0, sizeof(record));
  return valid;
}

static bool salt_write(uint32_t page, const uint8_t salt[32]) {
  salt_record_t record;
  memset(&record, 0xff, sizeof(record));
  memcpy(record.magic, SALT_MAGIC, sizeof(record.magic));
  record.version = SALT_VERSION;
  record.length = SALT_LENGTH;
  memcpy(record.salt, salt, SALT_LENGTH);
  salt_digest(&record, record.digest);
  record.commit = 0xffffffffu;

  bool ok = direct_erase_page(page) &&
            direct_write(page * PAGE_SIZE, &record,
                         offsetof(salt_record_t, commit));
  const uint32_t commit = SALT_COMMIT;
  ok = ok && direct_write(page * PAGE_SIZE + offsetof(salt_record_t, commit),
                          &commit, sizeof(commit));
  memset(&record, 0, sizeof(record));
  return ok && salt_valid(page, NULL);
}

void esp32s3_storage_init(void) {
  flash_mutex = xSemaphoreCreateRecursiveMutex();
  PORT_REQUIRE(flash_mutex != NULL, "flash mutex allocation");

  storage_partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, STORAGE_LABEL);
  if (storage_partition == NULL || storage_partition->size != STORAGE_SIZE ||
      storage_partition->address != 0xff0000u) {
    ESP_LOGE(TAG, "fixed 64 KiB storage partition not found");
    esp32s3_port_error("fixed storage partition missing", __FILE__, __LINE__);
  }
  PORT_CHECK(esp_partition_mmap(storage_partition, 0, STORAGE_SIZE,
                                     ESP_PARTITION_MMAP_DATA,
                                     (const void **)&storage_map,
                                     &storage_map_handle));
}

void esp32s3_storage_get_salt(uint8_t salt[32]) {
  if (!salt_loaded) {
    salt_record_t a;
    salt_record_t b;
    xSemaphoreTakeRecursive(flash_mutex, portMAX_DELAY);
    const bool va = salt_valid(SALT_PAGE_A, &a);
    const bool vb = salt_valid(SALT_PAGE_B, &b);

    if (va && vb) {
      if (memcmp(a.salt, b.salt, SALT_LENGTH) != 0) {
        xSemaphoreGiveRecursive(flash_mutex);
        __fatal_error("device salt copies conflict", __FILE__, __LINE__);
      }
      memcpy(persistent_salt, a.salt, SALT_LENGTH);
    } else if (va || vb) {
      const salt_record_t *good = va ? &a : &b;
      const uint32_t repair_page = va ? SALT_PAGE_B : SALT_PAGE_A;
      memcpy(persistent_salt, good->salt, SALT_LENGTH);
      if (!salt_write(repair_page, persistent_salt)) {
        xSemaphoreGiveRecursive(flash_mutex);
        __fatal_error("device salt repair failed", __FILE__, __LINE__);
      }
    } else {
      if (!is_erased(storage_map, STORAGE_SIZE)) {
        xSemaphoreGiveRecursive(flash_mutex);
        __fatal_error("storage has no valid device salt", __FILE__, __LINE__);
      }
      random_buffer(persistent_salt, SALT_LENGTH);
      if (!salt_write(SALT_PAGE_A, persistent_salt) ||
          !salt_write(SALT_PAGE_B, persistent_salt)) {
        xSemaphoreGiveRecursive(flash_mutex);
        __fatal_error("device salt creation failed", __FILE__, __LINE__);
      }
    }
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    salt_loaded = true;
    xSemaphoreGiveRecursive(flash_mutex);
  }
  memcpy(salt, persistent_salt, SALT_LENGTH);
}

/* Run before config_init as well as inside norcow_init. Recovery is idempotent;
 * only an explicit READY marker authorizes retiring the other area's data. */
void esp32s3_storage_validate_layout(void) {
  esp32s3_norcow_recover();
}

uint32_t flash_sector_size(uint16_t first_sector, uint16_t sector_count) {
  if (first_sector >= PAGE_COUNT || sector_count > PAGE_COUNT - first_sector) {
    return 0;
  }
  return (uint32_t)sector_count * PAGE_SIZE;
}

uint16_t flash_sector_find(uint16_t first_sector, uint32_t offset) {
  return (uint16_t)(first_sector + offset / PAGE_SIZE);
}

const void *flash_get_address(uint16_t sector, uint32_t offset, uint32_t size) {
  if (storage_map == NULL || sector >= PAGE_COUNT || offset > PAGE_SIZE ||
      size > PAGE_SIZE - offset) {
    return NULL;
  }
  return storage_map + sector * PAGE_SIZE + offset;
}

secbool flash_unlock_write(void) {
  if (flash_mutex == NULL ||
      xSemaphoreTakeRecursive(flash_mutex, portMAX_DELAY) != pdTRUE) {
    return secfalse;
  }
  unlock_depth++;
  return sectrue;
}

secbool flash_lock_write(void) {
  if (unlock_depth == 0) return secfalse;
  unlock_depth--;
  return xSemaphoreGiveRecursive(flash_mutex) == pdTRUE ? sectrue : secfalse;
}

static secbool checked_write(uint16_t sector, uint32_t offset,
                             const void *data, size_t size) {
  if (unlock_depth == 0 || sector == SALT_PAGE_A || sector == SALT_PAGE_B ||
      sector >= PAGE_COUNT || offset > PAGE_SIZE || size > PAGE_SIZE - offset) {
    return secfalse;
  }
  return direct_write(sector * PAGE_SIZE + offset, data, size) ? sectrue
                                                               : secfalse;
}

secbool flash_write_byte(uint16_t sector, uint32_t offset, uint8_t data) {
  if (unlock_depth == 0 || sector == SALT_PAGE_A || sector == SALT_PAGE_B ||
      sector >= PAGE_COUNT || offset >= PAGE_SIZE) {
    return secfalse;
  }
  const uint32_t aligned = offset & ~3u;
  uint32_t word;
  memcpy(&word, storage_map + sector * PAGE_SIZE + aligned, sizeof(word));
  ((uint8_t *)&word)[offset & 3u] = data;
  return checked_write(sector, aligned, &word, sizeof(word));
}

secbool flash_write_word(uint16_t sector, uint32_t offset, uint32_t data) {
  if ((offset & 3u) != 0) return secfalse;
  return checked_write(sector, offset, &data, sizeof(data));
}

secbool flash_write_block(uint16_t sector, uint32_t offset,
                          const flash_block_t block) {
  if ((offset & 3u) != 0) return secfalse;
  return checked_write(sector, offset, block, FLASH_BLOCK_SIZE);
}

secbool flash_write_burst(uint16_t sector, uint32_t offset,
                          const uint32_t *data) {
  (void)sector;
  (void)offset;
  (void)data;
  return secfalse;
}

secbool flash_sector_erase(uint16_t sector) {
  if (unlock_depth == 0 || sector == SALT_PAGE_A || sector == SALT_PAGE_B ||
      sector >= PAGE_COUNT) {
    return secfalse;
  }
  return direct_erase_page(sector) ? sectrue : secfalse;
}
