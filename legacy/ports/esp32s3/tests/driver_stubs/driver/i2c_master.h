#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct test_i2c_device *i2c_master_dev_handle_t;
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                              const uint8_t *data, size_t size, int timeout_ms);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t device, uint8_t *data,
                             size_t size, int timeout_ms);
