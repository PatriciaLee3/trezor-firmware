#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../main/esp32s3_storage.c"
#include "norcow.c"

static uint8_t disk[65536], baseline[65536], interrupted_disk[65536];
static const esp_partition_t partition = {0xff0000, 65536};
static jmp_buf interruption;
static bool can_jump;
static int trip = -1, checkpoints;
static unsigned fault_mode;
static unsigned long tested_cuts, recovery_cuts;

static void checkpoint(void) {
  if (checkpoints++ == trip) {
    assert(can_jump);
    longjmp(interruption, 1);
  }
}
void __fatal_error(const char *msg, const char *file, int line) {
  if (can_jump) longjmp(interruption, 2);
  fprintf(stderr, "unexpected fatal %s:%d: %s\n", file, line, msg);
  abort();
}
void esp32s3_port_error(const char *msg, const char *file, int line) {
  __fatal_error(msg, file, line);
}
void random_buffer(uint8_t *buf, size_t size) {
  /* Deterministic test-only salt. Never compiled into the ESP-IDF target. */
  for (size_t i = 0; i < size; i++) buf[i] = (uint8_t)(0xa0 + i);
}
const esp_partition_t *esp_partition_find_first(int type, int subtype,
                                                const char *label) {
  assert(type == 1 && subtype == 0x40 && strcmp(label, "trezor_storage") == 0);
  return &partition;
}
esp_err_t esp_partition_mmap(const esp_partition_t *p, size_t off, size_t size,
                             int type, const void **out, int *handle) {
  assert(p == &partition && off == 0 && size == sizeof(disk));
  (void)type;
  *out = disk;
  *handle = 1;
  return ESP_OK;
}
esp_err_t esp_partition_write(const esp_partition_t *p, size_t off,
                              const void *data, size_t size) {
  assert(p == &partition && off + size <= sizeof(disk) && (off % 4) == 0 &&
         (size % 4) == 0);
  checkpoint();
  const uint8_t *bytes = data;
  for (size_t j = 0; j < size; j++) {
    const size_t i = fault_mode == 2 ? size - 1 - j : j;
    assert((disk[off + i] & bytes[i]) == bytes[i]);
    if (fault_mode == 0) {
      disk[off + i] &= bytes[i];
    } else {
      /* A torn program is any prefix of the changed bits, in both orders. */
      for (unsigned b = 0; b < 8; b++) {
        const uint8_t bit = 1u << (fault_mode == 2 ? 7 - b : b);
        if ((disk[off + i] & bit) && !(bytes[i] & bit)) {
          disk[off + i] &= (uint8_t)~bit;
          checkpoint();
        }
      }
    }
  }
  checkpoint();
  return ESP_OK;
}
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t off,
                                    size_t size) {
  assert(p == &partition && off + size <= sizeof(disk) && off % 4096 == 0 &&
         size == 4096);
  checkpoint();
  const size_t chunk = fault_mode == 0 ? size / 2 : 512;
  for (size_t j = 0; j < size; j += chunk) {
    const size_t i = fault_mode == 2 ? size - chunk - j : j;
    memset(disk + off + i, 0xff, chunk);
    checkpoint();
  }
  return ESP_OK;
}
static void reboot(void) {
  trip = -1;
  unlock_depth = 0;
  salt_loaded = false;
  memset(persistent_salt, 0, sizeof(persistent_salt));
  esp32s3_storage_init();
}
static void load_salt(void) {
  uint8_t salt[32];
  esp32s3_storage_get_salt(salt);
}
static void expect_bad_salt(void) {
  reboot();
  can_jump = true;
  int reason = setjmp(interruption);
  if (reason == 0) {
    load_salt();
    assert(!"invalid salt accepted");
  }
  assert(reason == 2);
  can_jump = false;
}
static void expect_bad_layout(void) {
  uint8_t saved[65536];
  memcpy(saved, disk, sizeof(saved));
  can_jump = true;
  int reason = setjmp(interruption);
  if (reason == 0) {
    esp32s3_storage_validate_layout();
    assert(!"unsafe layout accepted");
  }
  assert(reason == 2);
  can_jump = false;
  assert(memcmp(saved, disk, sizeof(saved)) == 0);
}
static void basic_tests(void) {
  memset(disk, 0xff, sizeof(disk));
  reboot();
  load_salt();
  assert(salt_valid(0, NULL) && salt_valid(15, NULL));
  memcpy(baseline, disk, sizeof(disk));
  assert(flash_write_word(1, 0, 0) == secfalse);
  assert(flash_unlock_write() == sectrue);
  assert(flash_sector_erase(0) == secfalse &&
         flash_sector_erase(15) == secfalse);
  assert(flash_write_word(0, 0, 0) == secfalse &&
         flash_write_word(15, 0, 0) == secfalse);
  assert(flash_write_word(16, 0, 0) == secfalse &&
         flash_write_word(1, 4096, 0) == secfalse);
  assert(flash_write_word(1, 1, 0) == secfalse);
  assert(flash_write_word(1, 0, 0) == sectrue);
  assert(flash_write_word(1, 0, UINT32_MAX) == secfalse);
  assert(flash_get_address(16, 0, 0) == NULL &&
         flash_get_address(1, 4095, 2) == NULL);
  assert(flash_lock_write() == sectrue);
  memcpy(disk, baseline, sizeof(disk));
  memset(disk + 15 * 4096, 0xff, 4096);
  reboot();
  load_salt();
  assert(memcmp(disk, baseline, sizeof(disk)) == 0);
  uint8_t other[32];
  memset(other, 0x5a, sizeof(other));
  assert(salt_write(15, other));
  expect_bad_salt();
  memset(disk, 0xff, sizeof(disk));
  disk[4096] = 0;
  expect_bad_salt();

  /* Every salt write/erase boundary: either repair a valid copy or fail closed.
   */
  memcpy(disk, baseline, sizeof(disk));
  memset(disk + 15 * 4096, 0xff, 4096);
  reboot();
  checkpoints = 0;
  load_salt();
  int repair_steps = checkpoints;
  for (int stage = 0; stage < repair_steps; stage++) {
    memcpy(disk, baseline, sizeof(disk));
    memset(disk + 15 * 4096, 0xff, 4096);
    reboot();
    checkpoints = 0;
    trip = stage;
    can_jump = true;
    if (setjmp(interruption) == 0) load_salt();
    can_jump = false;
    reboot();
    load_salt();
    assert(memcmp(disk, baseline, sizeof(disk)) == 0);
  }
  memcpy(disk, baseline, sizeof(disk));
  reboot();
  load_salt();
  uint32_t version;
  norcow_init(&version);
  esp32s3_storage_validate_layout();
  norcow_wipe();
  assert(salt_valid(0, NULL) && salt_valid(15, NULL));
  memcpy(disk + 32768, disk + 4096, 8);
  expect_bad_layout();
  memset(disk + 32768, 0xff, 8);
  uint32_t future = ~(NORCOW_VERSION + 1u);
  memcpy(disk + 4100, &future, 4);
  expect_bad_layout();
  future = ~NORCOW_VERSION;
  memcpy(disk + 4100, &future, 4);
  esp32s3_storage_validate_layout();
  uint32_t magic = ESP32S3_NORCOW_READY;
  memcpy(disk + 4096, &magic, 4);
  memcpy(disk + 32768, disk + 4096, 8);
  expect_bad_layout();
  memset(disk + 32768, 0xff, 8);
  magic = ESP32S3_NORCOW_COMMITTED;
  memcpy(disk + 4096, &magic, 4);
  future = ~5u;
  memcpy(disk + 4100, &future, 4);
  expect_bad_layout();
  future = ~NORCOW_VERSION;
  memcpy(disk + 4100, &future, 4);
  /* A value crossing multiple physical pages must remain byte-exact. */
  uint8_t cross_page[8193];
  for (size_t i = 0; i < sizeof(cross_page); i++) cross_page[i] = (uint8_t)i;
  assert(norcow_set(9, cross_page, sizeof(cross_page)) == sectrue);
  compact();
  reboot();
  load_salt();
  norcow_init(&version);
  const void *value;
  uint16_t len;
  assert(norcow_get(9, &value, &len) == sectrue && len == sizeof(cross_page));
  assert(memcmp(value, cross_page, len) == 0);
  norcow_wipe();
  puts(
      "flash bounds, alignment, 1->0, salt conflict/repair, "
      "ambiguous/old/future headers and wipe retention passed");
}

static uint8_t sentinel[32], filler[32];
static uint32_t counter_value[3];
static uint32_t mounted_version;

static void mount_wallet(void) {
  load_salt();
  esp32s3_storage_validate_layout();
  norcow_init(&mounted_version);
}

static void assert_item(uint16_t key, const void *expected, uint16_t length) {
  const void *value = NULL;
  uint16_t len = 0;
  assert(norcow_get(key, &value, &len) == sectrue && len == length);
  assert(memcmp(value, expected, len) == 0);
}

static void assert_live(void) {
  assert_item(1, sentinel, sizeof(sentinel));
  assert_item(2, filler, sizeof(filler));
  assert_item(4, "", 0);
  assert_item(5, counter_value, sizeof(counter_value));
  assert(salt_valid(0, NULL) && salt_valid(15, NULL));
}

static bool has_ready(void) {
  uint32_t a, b;
  memcpy(&a, disk + 4096, 4);
  memcpy(&b, disk + 32768, 4);
  return a == ESP32S3_NORCOW_READY || b == ESP32S3_NORCOW_READY;
}

static void inject(void (*action)(void), int stage) {
  checkpoints = 0;
  trip = stage;
  can_jump = true;
  const int reason = setjmp(interruption);
  if (reason == 0) {
    action();
    assert(!"fault stage not reached");
  }
  assert(reason == 1);
  can_jump = false;
  trip = -1;
  tested_cuts++;
}

/* Cut recovery itself at EVERY checkpoint of each reachable READY snapshot,
 * then reboot again. This checks idempotence rather than just one clean reboot.
 */
static void recover_and_check(void (*check)(void)) {
  const bool committed_copy = has_ready();
  memcpy(interrupted_disk, disk, sizeof(disk));
  reboot();
  checkpoints = 0;
  mount_wallet();
  const int steps = checkpoints;
  check();
  if (committed_copy) {
    for (int stage = 0; stage < steps; stage++) {
      memcpy(disk, interrupted_disk, sizeof(disk));
      reboot();
      inject(mount_wallet, stage);
      recovery_cuts++;
      reboot();
      mount_wallet();
      check();
    }
  }
}

static void fixture(unsigned active, bool fill) {
  memset(disk, 0xff, sizeof(disk));
  reboot();
  mount_wallet();
  memset(sentinel, 0x42, sizeof(sentinel));
  memset(filler, 0xaa, sizeof(filler));
  assert(norcow_set(1, sentinel, sizeof(sentinel)) == sectrue);
  assert(norcow_set(2, filler, sizeof(filler)) == sectrue);
  assert(norcow_set(4, "", 0) == sectrue);
  assert(norcow_set_counter(5, 100) == sectrue);
  uint32_t count;
  for (unsigned i = 0; i < 7; i++)
    assert(norcow_next_counter(5, &count) == sectrue);
  assert(count == 107);
  counter_value[0] = 100;
  counter_value[1] = UINT32_MAX >> 7;
  counter_value[2] = UINT32_MAX;
  if (norcow_active_sector != active) compact();
  if (fill) {
    while (norcow_free_offset + 36 <= NORCOW_SECTOR_SIZE) {
      filler[0] ^= 0xff;
      assert(norcow_set(2, filler, sizeof(filler)) == sectrue);
    }
  }
  assert(norcow_active_sector == active);
  assert_live();
}

static void trigger_compaction(void) {
  assert(norcow_set(3, sentinel, 32) == sectrue);
}

static void compaction_tests(void) {
  for (fault_mode = 0; fault_mode < 3; fault_mode++) {
    for (unsigned active = 0; active < 2; active++) {
      fixture(active, true);
      memcpy(baseline, disk, sizeof(disk));
      checkpoints = 0;
      compact();
      int steps = checkpoints;
      assert_live();
      assert(norcow_active_sector == (active ^ 1u));
      for (int stage = 0; stage < steps; stage++) {
        memcpy(disk, baseline, sizeof(disk));
        reboot();
        mount_wallet();
        inject(compact, stage);
        recover_and_check(assert_live);
        /* Recovered storage remains writable, including another compaction. */
        compact();
        assert_live();
        trigger_compaction();
        assert_live();
        reboot();
        mount_wallet();
        assert_item(3, sentinel, 32);
      }
      /* Keep the original regression, including the subsequent key's append.
       * An interrupted append is not an atomic norcow_set transaction; only
       * already-committed, unrelated keys are asserted in this test. */
      memcpy(disk, baseline, sizeof(disk));
      reboot();
      mount_wallet();
      checkpoints = 0;
      trigger_compaction();
      steps = checkpoints;
      for (int stage = 0; stage < steps; stage++) {
        memcpy(disk, baseline, sizeof(disk));
        reboot();
        mount_wallet();
        inject(trigger_compaction, stage);
        reboot();
        mount_wallet();
        assert_live();
      }
      printf("compaction %u->%u, torn-operation mode %u passed\n", active,
             active ^ 1u, fault_mode);
    }
  }
}

static void assert_wiped(void) {
  const void *value;
  uint16_t len;
  for (unsigned key = 1; key <= 5; key++)
    assert(norcow_get(key, &value, &len) == secfalse);
  assert(salt_valid(0, NULL) && salt_valid(15, NULL));
  assert(flash_area_is_erased(&STORAGE_AREAS[norcow_active_sector ^ 1u]) ==
         sectrue);
}

static void assert_wipe_atomic(void) {
  const void *value;
  uint16_t len;
  if (norcow_get(1, &value, &len) == sectrue)
    assert_live();
  else
    assert_wiped();
}

static void initialization_and_wipe_tests(void) {
  for (fault_mode = 0; fault_mode < 3; fault_mode++) {
    memset(disk, 0xff, sizeof(disk));
    reboot();
    load_salt();
    memcpy(baseline, disk, sizeof(disk));
    checkpoints = 0;
    mount_wallet();
    const int init_steps = checkpoints;
    for (int stage = 0; stage < init_steps; stage++) {
      memcpy(disk, baseline, sizeof(disk));
      reboot();
      inject(mount_wallet, stage);
      recover_and_check(assert_wiped);
    }
    for (unsigned active = 0; active < 2; active++) {
      fixture(active, true);
      memcpy(baseline, disk, sizeof(disk));
      checkpoints = 0;
      norcow_wipe();
      const int steps = checkpoints;
      for (int stage = 0; stage < steps; stage++) {
        memcpy(disk, baseline, sizeof(disk));
        reboot();
        mount_wallet();
        inject(norcow_wipe, stage);
        recover_and_check(assert_wipe_atomic);
        norcow_wipe();
        reboot();
        mount_wallet();
        assert_wiped();
      }
    }
    printf("initialization/wipe, torn-operation mode %u passed\n", fault_mode);
  }
}

#ifdef TEST_NORCOW_UPGRADE
static void upgrade_wallet(void) {
  mount_wallet();
  if (mounted_version == NORCOW_VERSION) return;
  assert(mounted_version == 6);
  uint32_t offset = 0;
  uint16_t key, len;
  const void *value;
  while (norcow_get_next(&offset, &key, &value, &len) == sectrue)
    assert(norcow_set(key, value, len) == sectrue);
  /* Exercise lookups, in-place updates and streaming writes into PREPARED. */
  assert(norcow_set(3, NULL, 17) == sectrue);
  assert(norcow_update_bytes(3, sentinel, 7) == sectrue);
  assert(norcow_update_bytes(3, sentinel + 7, 10) == sectrue);
  uint32_t update = UINT32_MAX;
  assert(norcow_set(6, &update, sizeof(update)) == sectrue);
  update = 0x55555555;
  assert(norcow_set(6, &update, sizeof(update)) == sectrue);
  assert_live();  // Reads must still come from the old committed version.
  assert(norcow_upgrade_finish() == sectrue);
}

static void assert_upgrade_atomic(void) {
  assert_live();
  const void *value;
  uint16_t len;
  if (mounted_version == 6)
    assert(norcow_get(3, &value, &len) == secfalse);
  else {
    const uint32_t update = 0x55555555;
    assert(mounted_version == NORCOW_VERSION);
    assert_item(3, sentinel, 17);
    assert_item(6, &update, sizeof(update));
  }
}

static void upgrade_tests(void) {
  assert(NORCOW_VERSION ==
         7);  // Synthetic future version, never a firmware define.
  for (fault_mode = 0; fault_mode < 3; fault_mode++) {
    for (unsigned active = 0; active < 2; active++) {
      fixture(active, false);
      const uint32_t old_version = ~6u;
      memcpy(disk + (active == 0 ? 4096 : 32768) + 4, &old_version, 4);
      memcpy(baseline, disk, sizeof(disk));
      reboot();
      checkpoints = 0;
      upgrade_wallet();
      int steps = checkpoints;
      for (int stage = 0; stage < steps; stage++) {
        memcpy(disk, baseline, sizeof(disk));
        reboot();
        inject(upgrade_wallet, stage);
        recover_and_check(assert_upgrade_atomic);
        upgrade_wallet();
        reboot();
        mount_wallet();
        assert(mounted_version == 7);
        assert_live();
        assert_item(3, sentinel, 17);
      }
      /* A too-large migration must not compact over its only old-version copy.
       */
      memcpy(disk, baseline, sizeof(disk));
      reboot();
      mount_wallet();
      while (norcow_free_offset + 36 <= NORCOW_SECTOR_SIZE) {
        filler[0] ^= 0xff;
        assert(norcow_set(8, filler, 32) == sectrue);
      }
      uint8_t old_source[28 * 1024];
      memcpy(old_source, disk + (active == 0 ? 4096 : 32768),
             sizeof(old_source));
      can_jump = true;
      int reason = setjmp(interruption);
      if (reason == 0) {
        trigger_compaction();
        assert(!"overflowing upgrade accepted");
      }
      assert(reason == 2);
      can_jump = false;
      assert(memcmp(old_source, disk + (active == 0 ? 4096 : 32768),
                    sizeof(old_source)) == 0);
      printf(
          "synthetic version 6->7, area %u->%u, torn-operation mode %u "
          "passed\n",
          active, active ^ 1u, fault_mode);
    }
  }
}
#endif

int main(int argc, char **argv) {
  (void)argv;
#ifdef TEST_NORCOW_UPGRADE
  upgrade_tests();
#else
  basic_tests();
  if (argc > 1) return 0;
  compaction_tests();
  initialization_and_wipe_tests();
#endif
  printf("PASS: %lu injected power cuts, including %lu cuts during recovery\n",
         tested_cuts, recovery_cuts);
  return 0;
}
