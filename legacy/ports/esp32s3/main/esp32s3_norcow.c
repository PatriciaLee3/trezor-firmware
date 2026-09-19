#include "esp32s3_norcow.h"

#include <stdbool.h>
#include <string.h>

#include "common.h"
#include "flash_area.h"
#include "norcow.h"

/* Two equal-sized areas; no journal in the salt pages and no loss of capacity.
 * These offsets are part of the existing bitwise NORCOW format. */
_Static_assert(NORCOW_SECTOR_COUNT == 2 && NORCOW_HEADER_LEN == 0 &&
                   FLASH_BLOCK_WORDS == 1,
               "unsupported NORCOW geometry");
#define HEADER_SIZE 8u
#define FIRST_PORT_VERSION 6u

static const uint8_t *area_bytes(uint8_t area) {
  ensure(sectrue * (area < NORCOW_SECTOR_COUNT), "invalid NORCOW area");
  const uint8_t *p =
      flash_area_get_address(&STORAGE_AREAS[area], 0, NORCOW_SECTOR_SIZE);
  ensure(sectrue * (p != NULL), "NORCOW mapping failed");
  return p;
}

static uint32_t header_word(uint8_t area, unsigned word) {
  uint32_t result;
  memcpy(&result, area_bytes(area) + word * sizeof(result), sizeof(result));
  return result;
}

static bool erased_bytes(const uint8_t *p, uint32_t len) {
  for (uint32_t i = 0; i < len; i++)
    if (p[i] != 0xff) return false;
  return true;
}

static void write_header(uint8_t area, uint32_t offset, uint32_t value) {
  ensure(flash_unlock_write(), NULL);
  ensure(flash_area_write_word(&STORAGE_AREAS[area], offset, value),
         "NORCOW header write failed");
  ensure(flash_lock_write(), NULL);
  ensure(sectrue * (header_word(area, offset / 4) == value),
         "NORCOW header verification failed");
}

static void check_version(uint8_t area) {
  const uint32_t version = ~header_word(area, 1);
  ensure(sectrue * (version >= FIRST_PORT_VERSION && version <= NORCOW_VERSION),
         "unsupported NORCOW version; preserve flash");
}

/* READY is the durable commit point. No application writes may happen before
 * the old area is entirely erased and READY has become standard NRC2. Repeating
 * this cleanup after another power loss is safe, in either compaction
 * direction. A torn old-area erase cannot make us select that area's damaged
 * contents. */
static void finish_ready(uint8_t destination) {
  const uint8_t source = destination ^ 1u;
  ensure(sectrue * (header_word(destination, 0) == ESP32S3_NORCOW_READY),
         "missing NORCOW commit point");
  if (sectrue != flash_area_is_erased(&STORAGE_AREAS[source])) {
    write_header(source, 0, 0);  // Retire before erasing any payload page.
    ensure(flash_area_erase(&STORAGE_AREAS[source], NULL),
           "NORCOW retired area erase failed");
    ensure(flash_area_is_erased(&STORAGE_AREAS[source]),
           "NORCOW retired area verification failed");
  }
  write_header(destination, 0, ESP32S3_NORCOW_COMMITTED);
}

void esp32s3_norcow_prepare(uint8_t destination) {
  (void)area_bytes(destination);  // Check before indexing STORAGE_AREAS.
  ensure(flash_area_erase(&STORAGE_AREAS[destination], NULL),
         "NORCOW destination erase failed");
  ensure(flash_area_is_erased(&STORAGE_AREAS[destination]),
         "NORCOW destination verification failed");
  write_header(destination, 4, ~NORCOW_VERSION);
  write_header(destination, 0, ESP32S3_NORCOW_PREPARED);
}

void esp32s3_norcow_commit(uint8_t source, uint8_t destination) {
  ensure(sectrue * (source < 2 && destination < 2 && source != destination),
         "invalid NORCOW commit areas");
  ensure(sectrue * (header_word(destination, 0) == ESP32S3_NORCOW_PREPARED &&
                    header_word(destination, 1) == ~NORCOW_VERSION),
         "invalid NORCOW prepared header");
  write_header(destination, 0, ESP32S3_NORCOW_READY);
  finish_ready(destination);
}

void esp32s3_norcow_recover(void) {
  int committed = -1, ready = -1;
  for (uint8_t i = 0; i < 2; i++) {
    const uint32_t magic = header_word(i, 0);
    if (magic == ESP32S3_NORCOW_COMMITTED || magic == ESP32S3_NORCOW_READY ||
        magic == ESP32S3_NORCOW_PREPARED) {
      check_version(i);
    }
    if (magic == ESP32S3_NORCOW_COMMITTED) {
      ensure(sectrue * (committed == -1),
             "ambiguous NORCOW headers; preserve flash");
      committed = i;
    } else if (magic == ESP32S3_NORCOW_READY) {
      ensure(sectrue * (ready == -1),
             "ambiguous NORCOW commit points; preserve flash");
      ready = i;
    }
  }
  if (ready != -1) {
    if (committed != -1) {
      ensure(sectrue * (~header_word(ready, 1) >= ~header_word(committed, 1)),
             "NORCOW commit version conflict; preserve flash");
    }
    finish_ready((uint8_t)ready);
    return;
  }
  if (committed != -1) return;  // PREPARED is never a boot-time source.

  /* A first initialization can lose power before its empty header is committed.
   * Retry ONLY if both payloads are erased and headers are erased or monotonic
   * prefixes of the current PREPARED header. Arbitrary nonempty data is fatal.
   */
  for (uint8_t i = 0; i < 2; i++) {
    const uint32_t magic = header_word(i, 0);
    const uint32_t version = header_word(i, 1);
    ensure(sectrue *
               (erased_bytes(area_bytes(i) + HEADER_SIZE,
                             NORCOW_SECTOR_SIZE - HEADER_SIZE) &&
                (magic & ESP32S3_NORCOW_PREPARED) == ESP32S3_NORCOW_PREPARED &&
                (version & ~NORCOW_VERSION) == ~NORCOW_VERSION),
           "unrecognized NORCOW data; preserve flash");
  }
}
