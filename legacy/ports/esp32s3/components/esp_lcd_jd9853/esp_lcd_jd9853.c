/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_lcd_jd9853.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "jd9853";
typedef struct {
  esp_lcd_panel_t base;
  esp_lcd_panel_io_handle_t io; /* Borrowed from the caller. */
  gpio_num_t reset_gpio;
  bool reset_level;
  int x_gap, y_gap;
  uint8_t bytes_per_pixel;
  uint8_t madctl;
  uint8_t colmod;
  const jd9853_lcd_init_cmd_t *init_cmds;
  size_t init_count;
  bool custom_init;
} jd9853_t;

/* Waveshare 172x320 module timing and analog setup. Keep board tuning here;
 * orientation, viewport offsets and SPI transport remain caller controlled. */
static const jd9853_lcd_init_cmd_t default_init[] = {
    {0x11, NULL, 0, 120},
    {0xDF, (const uint8_t[]){0x98, 0x53}, 2, 0},
    {0xDF, (const uint8_t[]){0x98, 0x53}, 2, 0},
    {0xB2, (const uint8_t[]){0x23}, 1, 0},
    {0xB7, (const uint8_t[]){0x00, 0x47, 0x00, 0x6F}, 4, 0},
    {0xBB, (const uint8_t[]){0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0}, 6, 0},
    {0xC0, (const uint8_t[]){0x44, 0xA4}, 2, 0},
    {0xC1, (const uint8_t[]){0x16}, 1, 0},
    {0xC3, (const uint8_t[]){0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77}, 8,
     0},
    {0xC4,
     (const uint8_t[]){0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B,
                       0x0A, 0x16, 0x82},
     12, 0},  // 00=60Hz 06=57Hz 08=51Hz, LN=320 Line
    {0xC8, (const uint8_t[]){0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
                             0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
                             0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
                             0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00},
     32, 0},  // SET_R_GAMMA
    {0xD0, (const uint8_t[]){0x04, 0x06, 0x6B, 0x0F, 0x00}, 5, 0},
    {0xD7, (const uint8_t[]){0x00, 0x30}, 2, 0},
    {0xE6, (const uint8_t[]){0x14}, 1, 0},
    {0xDE, (const uint8_t[]){0x01}, 1, 0},
    {0xB7, (const uint8_t[]){0x03, 0x13, 0xEF, 0x35, 0x35}, 5, 0},
    {0xC1, (const uint8_t[]){0x14, 0x15, 0xC0}, 3, 0},
    {0xC2, (const uint8_t[]){0x06, 0x3A}, 2, 0},
    {0xC4, (const uint8_t[]){0x72, 0x12}, 2, 0},
    {0xBE, (const uint8_t[]){0x00}, 1, 0},
    {0xDE, (const uint8_t[]){0x02}, 1, 0},
    {0xE5, (const uint8_t[]){0x00, 0x02, 0x00}, 3, 0},
    {0xE5, (const uint8_t[]){0x01, 0x02, 0x00}, 3, 0},
    {0xDE, (const uint8_t[]){0x00}, 1, 0},
    {0x35, (const uint8_t[]){0x00}, 1, 0},
    {0x3A, (const uint8_t[]){0x05}, 1, 0},  // 06=RGB666；05=RGB565
    {0x2A, (const uint8_t[]){0x00, 0x22, 0x00, 0xCD}, 4,
     0},  // Start_X=34, End_X=205
    {0x2B, (const uint8_t[]){0x00, 0x00, 0x01, 0x3F}, 4,
     0},  // Start_Y=0, End_Y=319
    {0xDE, (const uint8_t[]){0x02}, 1, 0},
    {0xE5, (const uint8_t[]){0x00, 0x02, 0x00}, 3, 0},
    {0xDE, (const uint8_t[]){0x00}, 1, 0},
    {0x29, NULL, 0, 0},
};

static esp_err_t validate_init(const jd9853_t *lcd) {
  for (size_t i = 0; i < lcd->init_count; i++) {
    const jd9853_lcd_init_cmd_t *cmd = &lcd->init_cmds[i];
    if (cmd->cmd < 0 || cmd->cmd > UINT8_MAX ||
        (cmd->data_bytes != 0 && cmd->data == NULL))
      return ESP_ERR_INVALID_ARG;
    if (cmd->cmd == LCD_CMD_MADCTL || cmd->cmd == LCD_CMD_COLMOD) {
      if (cmd->data_bytes != 1) return ESP_ERR_INVALID_ARG;
      if (cmd->cmd == LCD_CMD_COLMOD && lcd->custom_init) {
        const uint8_t format = *(const uint8_t *)cmd->data;
        if (format != lcd->colmod && format != (lcd->colmod & 0x0f))
          return ESP_ERR_NOT_SUPPORTED;
      }
    }
  }
  return ESP_OK;
}

static esp_err_t jd9853_delete(esp_lcd_panel_t *panel) {
  jd9853_t *lcd = (jd9853_t *)panel;
  if (lcd->reset_gpio != GPIO_NUM_NC) {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(lcd->reset_gpio), TAG,
                        "release reset pin");
  }
  heap_caps_free(lcd);
  return ESP_OK;
}

static esp_err_t jd9853_reset(esp_lcd_panel_t *panel) {
  jd9853_t *lcd = (jd9853_t *)panel;
  if (lcd->reset_gpio != GPIO_NUM_NC) {
    ESP_RETURN_ON_ERROR(gpio_set_level(lcd->reset_gpio, lcd->reset_level), TAG,
                        "assert reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(lcd->reset_gpio, !lcd->reset_level), TAG,
                        "release reset");
    vTaskDelay(pdMS_TO_TICKS(10));
  } else {
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(lcd->io, LCD_CMD_SWRESET, NULL, 0), TAG,
        "soft reset");
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return ESP_OK;
}

static esp_err_t jd9853_init(esp_lcd_panel_t *panel) {
  jd9853_t *lcd = (jd9853_t *)panel;
  ESP_RETURN_ON_ERROR(validate_init(lcd), TAG,
                      "invalid initialization sequence");
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_io_tx_param(lcd->io, LCD_CMD_SLPOUT, NULL, 0), TAG,
      "exit sleep");
  vTaskDelay(pdMS_TO_TICKS(100));
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_io_tx_param(lcd->io, LCD_CMD_MADCTL, &lcd->madctl, 1), TAG,
      "MADCTL");
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_io_tx_param(lcd->io, LCD_CMD_COLMOD, &lcd->colmod, 1), TAG,
      "COLMOD");
  for (size_t i = 0; i < lcd->init_count; i++) {
    const jd9853_lcd_init_cmd_t *cmd = &lcd->init_cmds[i];
    const void *data = cmd->data;
    /* The default sequence's COLMOD must follow the requested pixel format. */
    const uint8_t format = lcd->colmod & 0x0f;
    if (!lcd->custom_init && cmd->cmd == LCD_CMD_COLMOD) data = &format;
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(lcd->io, cmd->cmd, data, cmd->data_bytes),
        TAG, "initialization command");
    if (cmd->cmd == LCD_CMD_MADCTL) lcd->madctl = *(const uint8_t *)data;
    if (cmd->delay_ms != 0) vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
  }
  return ESP_OK;
}

static esp_err_t jd9853_draw(esp_lcd_panel_t *panel, int x_start, int y_start,
                             int x_end, int y_end, const void *pixels) {
  jd9853_t *lcd = (jd9853_t *)panel;
  if (pixels == NULL || x_start < 0 || y_start < 0 || x_start >= x_end ||
      y_start >= y_end)
    return ESP_ERR_INVALID_ARG;
  const int64_t xs = (int64_t)x_start + lcd->x_gap;
  const int64_t ys = (int64_t)y_start + lcd->y_gap;
  const int64_t xe = (int64_t)x_end + lcd->x_gap;
  const int64_t ye = (int64_t)y_end + lcd->y_gap;
  if (xs < 0 || ys < 0 || xe > UINT16_MAX + INT64_C(1) ||
      ye > UINT16_MAX + INT64_C(1))
    return ESP_ERR_INVALID_ARG;
  // ESP-IDF target transports use 32-bit byte lengths.
  const uint32_t width = xe - xs, height = ye - ys;
  if (width > UINT32_MAX / height / lcd->bytes_per_pixel)
    return ESP_ERR_INVALID_ARG;
  const size_t length = width * height * lcd->bytes_per_pixel;
  const uint8_t columns[] = {xs >> 8, xs, (xe - 1) >> 8, xe - 1};
  const uint8_t rows[] = {ys >> 8, ys, (ye - 1) >> 8, ye - 1};
  ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(lcd->io, LCD_CMD_CASET, columns,
                                                sizeof(columns)),
                      TAG, "column window");
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_io_tx_param(lcd->io, LCD_CMD_RASET, rows, sizeof(rows)),
      TAG, "row window");
  /* IDF retains pixels until its completion callback. Do not copy or wait. */
  return esp_lcd_panel_io_tx_color(lcd->io, LCD_CMD_RAMWR, pixels, length);
}

static esp_err_t write_madctl(jd9853_t *lcd, uint8_t value) {
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_io_tx_param(lcd->io, LCD_CMD_MADCTL, &value, 1), TAG,
      "MADCTL");
  lcd->madctl = value;
  return ESP_OK;
}

static esp_err_t jd9853_mirror(esp_lcd_panel_t *panel, bool x, bool y) {
  jd9853_t *lcd = (jd9853_t *)panel;
  const uint8_t value = (lcd->madctl & ~(LCD_CMD_MX_BIT | LCD_CMD_MY_BIT)) |
                        (x ? LCD_CMD_MX_BIT : 0) | (y ? LCD_CMD_MY_BIT : 0);
  return write_madctl(lcd, value);
}

static esp_err_t jd9853_swap(esp_lcd_panel_t *panel, bool swap) {
  jd9853_t *lcd = (jd9853_t *)panel;
  return write_madctl(
      lcd, (lcd->madctl & ~LCD_CMD_MV_BIT) | (swap ? LCD_CMD_MV_BIT : 0));
}

static esp_err_t jd9853_gap(esp_lcd_panel_t *panel, int x, int y) {
  jd9853_t *lcd = (jd9853_t *)panel;
  lcd->x_gap = x;
  lcd->y_gap = y;
  return ESP_OK;
}

static esp_err_t jd9853_invert(esp_lcd_panel_t *panel, bool invert) {
  return esp_lcd_panel_io_tx_param(((jd9853_t *)panel)->io,
                                   invert ? LCD_CMD_INVON : LCD_CMD_INVOFF,
                                   NULL, 0);
}

static esp_err_t jd9853_display(esp_lcd_panel_t *panel, bool on) {
  return esp_lcd_panel_io_tx_param(
      ((jd9853_t *)panel)->io, on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
}

esp_err_t esp_lcd_new_panel_jd9853(esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t *config,
                                   esp_lcd_panel_handle_t *out_panel) {
  if (out_panel == NULL) return ESP_ERR_INVALID_ARG;
  *out_panel = NULL;
  if (io == NULL || config == NULL ||
      (config->reset_gpio_num != GPIO_NUM_NC &&
       !GPIO_IS_VALID_OUTPUT_GPIO(config->reset_gpio_num)))
    return ESP_ERR_INVALID_ARG;
  if ((config->bits_per_pixel != 16 && config->bits_per_pixel != 18) ||
      (config->rgb_ele_order != LCD_RGB_ELEMENT_ORDER_RGB &&
       config->rgb_ele_order != LCD_RGB_ELEMENT_ORDER_BGR) ||
      config->data_endian != LCD_RGB_DATA_ENDIAN_BIG)
    return ESP_ERR_NOT_SUPPORTED;
  jd9853_t *lcd = heap_caps_calloc(1, sizeof(*lcd), MALLOC_CAP_DEFAULT);
  if (lcd == NULL) return ESP_ERR_NO_MEM;
  lcd->io = io;
  lcd->reset_gpio = config->reset_gpio_num;
  lcd->reset_level = config->flags.reset_active_high;
  lcd->bytes_per_pixel = config->bits_per_pixel == 16 ? 2 : 3;
  lcd->colmod = config->bits_per_pixel == 16 ? 0x55 : 0x66;
  lcd->madctl =
      config->rgb_ele_order == LCD_RGB_ELEMENT_ORDER_BGR ? LCD_CMD_BGR_BIT : 0;
  lcd->init_cmds = default_init;
  lcd->init_count = sizeof(default_init) / sizeof(default_init[0]);
  if (config->vendor_config != NULL) {
    const jd9853_vendor_config_t *vendor = config->vendor_config;
    if (vendor->init_cmds == NULL && vendor->init_cmds_size != 0) {
      heap_caps_free(lcd);
      return ESP_ERR_INVALID_ARG;
    }
    if (vendor->init_cmds != NULL) {
      lcd->init_cmds = vendor->init_cmds;
      lcd->init_count = vendor->init_cmds_size;
      lcd->custom_init = true;
    }
  }
  esp_err_t ret = validate_init(lcd);
  if (ret != ESP_OK) {
    heap_caps_free(lcd);
    return ret;
  }
  if (lcd->reset_gpio != GPIO_NUM_NC) {
    const gpio_config_t gpio = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = UINT64_C(1) << lcd->reset_gpio,
    };
    ret = gpio_config(&gpio);
    if (ret != ESP_OK) {
      heap_caps_free(lcd);
      return ret;
    }
  }
  lcd->base = (esp_lcd_panel_t){
      .del = jd9853_delete,
      .reset = jd9853_reset,
      .init = jd9853_init,
      .draw_bitmap = jd9853_draw,
      .invert_color = jd9853_invert,
      .set_gap = jd9853_gap,
      .mirror = jd9853_mirror,
      .swap_xy = jd9853_swap,
      .disp_on_off = jd9853_display,
  };
  *out_panel = &lcd->base;
  return ESP_OK;
}
