#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { uint32_t address, size; } esp_partition_t;
typedef int esp_partition_subtype_t;
typedef int esp_partition_mmap_handle_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_MMAP_DATA 0
const esp_partition_t *esp_partition_find_first(int type, esp_partition_subtype_t subtype, const char *label);
esp_err_t esp_partition_mmap(const esp_partition_t *p, size_t offset, size_t size,
                            int type, const void **out, esp_partition_mmap_handle_t *handle);
esp_err_t esp_partition_write(const esp_partition_t *p, size_t offset, const void *data, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t offset, size_t size);
