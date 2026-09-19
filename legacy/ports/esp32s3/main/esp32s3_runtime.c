#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "blake2s.h"
#include "bootloader_random.h"
#include "common.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_image_format.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "memory.h"
#include "sha2.h"
#include "util.h"

#include "esp32s3_storage.h"
#include "port_error.h"
#include "port_logic.h"

#define FACTORY_PARTITION_SIZE 0x400000u
#define BOOTLOADER_OFFSET 0x0000u
#define BOOTLOADER_SIZE CONFIG_PARTITION_TABLE_OFFSET

static SemaphoreHandle_t rng_mutex;
uint32_t legacy_stack_chk_guard;

bool esp32s3_display_ready(void);
void esp32s3_usb_disconnect(void);

void esp32s3_port_error(const char *message, const char *file, int line) {
  static bool failing;
  esp32s3_usb_disconnect();
  if (!failing && esp32s3_display_ready()) {
    failing = true;
    __fatal_error(message, file, line);
  }
  shutdown();
}

void setup(void) {
  rng_mutex = xSemaphoreCreateMutex();
  PORT_REQUIRE(rng_mutex != NULL, "RNG mutex allocation");
  esp32s3_storage_init();
}

void setupApp(void) { setup(); }
void mpu_config_off(void) {}
void mpu_config_bootloader(void) {}
void mpu_config_firmware(void) {}
void timer_init(void) {}

uint32_t timer_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000u); }
uint32_t svc_timer_ms(void) { return timer_ms(); }

void random_buffer(uint8_t *buf, size_t len) {
  if (buf == NULL || len == 0) return;
  xSemaphoreTake(rng_mutex, portMAX_DELAY);
  bootloader_random_enable();
  esp_fill_random(buf, len);
  bootloader_random_disable();
  xSemaphoreGive(rng_mutex);
}

void esp32s3_get_hw_entropy(uint8_t entropy[HW_ENTROPY_LEN]) {
  static const uint8_t domain[] = "EspTrezor-T1 UID";
  uint8_t mac[6];
  uint8_t digest[SHA256_DIGEST_LENGTH];
  esp_chip_info_t chip;
  SHA256_CTX ctx;
  PORT_CHECK(esp_efuse_mac_get_default(mac));
  esp_chip_info(&chip);
  sha256_Init(&ctx);
  sha256_Update(&ctx, domain, sizeof(domain) - 1);
  sha256_Update(&ctx, mac, sizeof(mac));
  sha256_Update(&ctx, (const uint8_t *)&chip.revision, sizeof(chip.revision));
  sha256_Final(&ctx, digest);
  memcpy(entropy, digest, 12);
  esp32s3_storage_get_salt(entropy + 12);
  esp32s3_storage_validate_layout();
  memset(mac, 0, sizeof(mac));
  memset(digest, 0, sizeof(digest));
}

void memory_protect(void) {}
void memory_write_unlock(void) {}

/* IDF's partition safety check needs this read-only query. Resolve the only
 * executable partition without linking esp_ota_ops or any updater code. */
const esp_partition_t *__wrap_esp_ota_get_running_partition(void) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                  ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
}

int memory_bootloader_hash(uint8_t *hash) {
  uint32_t image_size = 0;
  if (esp_image_verify_bootloader(&image_size) != ESP_OK ||
      image_size == 0 || image_size > BOOTLOADER_SIZE) return 0;
  uint8_t chunk[1024];
  SHA256_CTX ctx;
  sha256_Init(&ctx);
  for (uint32_t offset = 0; offset < image_size; offset += sizeof(chunk)) {
    const uint32_t length =
        image_size - offset < sizeof(chunk) ? image_size - offset
                                                 : sizeof(chunk);
    if (esp_flash_read(NULL, chunk, BOOTLOADER_OFFSET + offset, length) !=
        ESP_OK) {
      memset(chunk, 0, sizeof(chunk));
      return 0;
    }
    sha256_Update(&ctx, chunk, length);
  }
  sha256_Final(&ctx, hash);
  sha256_Raw(hash, SHA256_DIGEST_LENGTH, hash);
  memset(chunk, 0, sizeof(chunk));
  return SHA256_DIGEST_LENGTH;
}

int memory_firmware_hash(const uint8_t *challenge, uint32_t challenge_size,
                         void (*progress_callback)(uint32_t, uint32_t),
                         uint8_t hash[BLAKE2S_DIGEST_LENGTH]) {
  const esp_partition_t *factory = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
  if (factory == NULL || factory->size != FACTORY_PARTITION_SIZE) return 1;

  const uint8_t *mapped = NULL;
  esp_partition_mmap_handle_t handle;
  if (esp_partition_mmap(factory, 0, factory->size, ESP_PARTITION_MMAP_DATA,
                         (const void **)&mapped, &handle) != ESP_OK) {
    return 1;
  }

  BLAKE2S_CTX ctx;
  int result = challenge_size
                   ? blake2s_InitKey(&ctx, BLAKE2S_DIGEST_LENGTH, challenge,
                                     challenge_size)
                   : blake2s_Init(&ctx, BLAKE2S_DIGEST_LENGTH);
  if (result == 0) {
    const uint32_t chunk = 64 * 1024;
    for (uint32_t offset = 0; offset < factory->size; offset += chunk) {
      result = blake2s_Update(&ctx, mapped + offset, chunk);
      if (result != 0) break;
      if (progress_callback != NULL) {
        progress_callback(offset / chunk, factory->size / chunk);
      }
      vTaskDelay(1);
    }
  }
  if (result == 0) result = blake2s_Final(&ctx, hash, BLAKE2S_DIGEST_LENGTH);
  esp_partition_munmap(handle);
  return result;
}

void svc_flash_unlock(void) {}
void svc_flash_program(uint32_t program_size) { (void)program_size; }
void svc_flash_erase_sector(uint8_t sector) { (void)sector; }
uint32_t svc_flash_lock(void) { return 0; }

void __attribute__((noreturn)) shutdown(void) {
  esp32s3_usb_disconnect();
  for (;;) vTaskDelay(portMAX_DELAY);
}
