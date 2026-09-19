/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP LCD touch: AXS5106
 */

#pragma once

#include "driver/i2c_master.h"
#include "esp_lcd_touch.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new AXS5106 touch driver
 *
 * @note The caller owns dev_handle and serializes sampling, coordinate
 * consumption and lifecycle operations. GPIOs are exclusively owned by this
 * instance until deletion. With interrupts, register callbacks and delete on
 * the GPIO ISR service's core, after stopping other users of the handle.
 *
 * The driver returns raw coordinates; esp_lcd_touch applies its configured
 * transforms. Each get consumes the latest successful sample. Failed reads
 * invalidate it. Multi-touch rejection requires capacity for two points.
 *
 * Delete unregisters interrupts and releases driver-owned GPIOs and memory,
 * but never removes the I2C device or shared ISR service. A failed delete
 * retains the instance for retry. Creation failure sets out_touch to NULL.
 *
 * @param dev_handle Initialized I2C device handle
 * @param config: Touch configuration
 * @param out_touch: Touch instance handle
 * @return
 *      - ESP_OK                    on success
 *      - ESP_ERR_NO_MEM            if allocation fails
 *      - ESP_ERR_INVALID_ARG       if handles or GPIO configuration are invalid
 *      - GPIO/reset/interrupt errors are propagated without translation
 */
esp_err_t esp_lcd_touch_new_i2c_axs5106(i2c_master_dev_handle_t dev_handle,
                                        const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *out_touch);

/**
 * @brief I2C address of the AXS5106 controller
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS (0x63)

/**
 * @brief Touch IO configuration structure
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_AXS5106_CONFIG()                        \
  {                                                                  \
    .dev_addr = ESP_LCD_TOUCH_IO_I2C_AXS5106_ADDRESS,                \
    .control_phase_bytes = 1, .dc_bit_offset = 0, .lcd_cmd_bits = 8, \
    .flags = {                                                       \
      .disable_control_phase = 1,                                    \
    }                                                                \
  }

#ifdef __cplusplus
}
#endif
