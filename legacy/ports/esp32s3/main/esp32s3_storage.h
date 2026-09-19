#ifndef ESP32S3_STORAGE_H
#define ESP32S3_STORAGE_H

#include <stddef.h>
#include <stdint.h>

void esp32s3_storage_init(void);
void esp32s3_storage_get_salt(uint8_t salt[32]);
void esp32s3_storage_validate_layout(void);

#endif
