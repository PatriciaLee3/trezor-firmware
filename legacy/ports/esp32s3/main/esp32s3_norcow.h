#ifndef ESP32S3_NORCOW_H
#define ESP32S3_NORCOW_H

#include <stdint.h>

/* Only transient magic words differ from upstream NRC2. Both transitions to
 * READY and then COMMITTED clear one bit, after all preceding writes verify. */
#define ESP32S3_NORCOW_COMMITTED UINT32_C(0x3243524e)
#define ESP32S3_NORCOW_PREPARED UINT32_C(0xf243524e)
#define ESP32S3_NORCOW_READY UINT32_C(0xb243524e)

void esp32s3_norcow_recover(void);
void esp32s3_norcow_prepare(uint8_t destination);
void esp32s3_norcow_commit(uint8_t source, uint8_t destination);

#endif
