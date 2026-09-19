/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_lcd_touch_axs5106.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "freertos/task.h"

enum {
  AXS5106_REPORT_REG = 0x01,
  AXS5106_REPORT_SIZE = 14,
  AXS5106_MAX_POINTS = 2,
  AXS5106_TIMEOUT_MS = 100
};
static const char *TAG = "axs5106";

typedef struct {
  esp_lcd_touch_t base;
  i2c_master_dev_handle_t device; /* Borrowed from the caller. */
  bool reset_configured;
  bool interrupt_configured;
} axs5106_t;

typedef struct {
  uint16_t x, y;
} axs5106_point_t;

static esp_err_t decode_report(const uint8_t report[AXS5106_REPORT_SIZE],
                               axs5106_point_t points[AXS5106_MAX_POINTS],
                               uint8_t *count) {
  *count = report[1] & 0x0f;
  if (*count > AXS5106_MAX_POINTS) return ESP_ERR_INVALID_RESPONSE;
  for (uint8_t i = 0; i < *count; i++) {
    const uint8_t *p = &report[2 + i * 6];
    points[i].x = ((uint16_t)(p[0] & 0x0f) << 8) | p[1];
    points[i].y = ((uint16_t)(p[2] & 0x0f) << 8) | p[3];
  }
  return ESP_OK;
}

static esp_err_t axs5106_read(esp_lcd_touch_handle_t tp) {
  if (tp == NULL) return ESP_ERR_INVALID_ARG;
  axs5106_t *touch = (axs5106_t *)tp;
  portENTER_CRITICAL(&tp->data.lock);
  tp->data.points = 0;
  portEXIT_CRITICAL(&tp->data.lock);
  const uint8_t reg = AXS5106_REPORT_REG;
  uint8_t report[AXS5106_REPORT_SIZE];
  ESP_RETURN_ON_ERROR(
      i2c_master_transmit(touch->device, &reg, sizeof(reg), AXS5106_TIMEOUT_MS),
      TAG, "select report register");
  /* The controller needs Twr > 45 us after the write STOP to prepare data. */
  esp_rom_delay_us(2000);
  ESP_RETURN_ON_ERROR(i2c_master_receive(touch->device, report, sizeof(report),
                                         AXS5106_TIMEOUT_MS),
                      TAG, "read report");
  axs5106_point_t points[AXS5106_MAX_POINTS];
  uint8_t count;
  ESP_RETURN_ON_ERROR(decode_report(report, points, &count), TAG,
                      "invalid touch report");
  const size_t capacity = sizeof(tp->data.coords) / sizeof(tp->data.coords[0]);
  if (count > capacity) count = capacity;
  portENTER_CRITICAL(&tp->data.lock);
  for (uint8_t i = 0; i < count; i++) {
    tp->data.coords[i].x = points[i].x;
    tp->data.coords[i].y = points[i].y;
    tp->data.coords[i].strength = 0;
  }
  tp->data.points = count;
  portEXIT_CRITICAL(&tp->data.lock);
  return ESP_OK;
}

static bool axs5106_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                           uint16_t *strength, uint8_t *count,
                           uint8_t capacity) {
  if (count == NULL) return false;
  *count = 0;
  if (tp == NULL || x == NULL || y == NULL || capacity == 0) return false;
  portENTER_CRITICAL(&tp->data.lock);
  *count = tp->data.points < capacity ? tp->data.points : capacity;
  for (uint8_t i = 0; i < *count; i++) {
    x[i] = tp->data.coords[i].x;
    y[i] = tp->data.coords[i].y;
    if (strength != NULL) strength[i] = 0;
  }
  tp->data.points = 0;
  portEXIT_CRITICAL(&tp->data.lock);
  return *count != 0;
}

static esp_err_t axs5106_delete(esp_lcd_touch_handle_t tp) {
  if (tp == NULL) return ESP_ERR_INVALID_ARG;
  axs5106_t *touch = (axs5106_t *)tp;
  if (touch->interrupt_configured) {
    ESP_RETURN_ON_ERROR(gpio_intr_disable(tp->config.int_gpio_num), TAG,
                        "disable interrupt");
    /* The public framework can register/unregister callbacks after creation.
     * Remove by owned pin, even if a failed unregister already cleared config.
     */
    const esp_err_t removed = gpio_isr_handler_remove(tp->config.int_gpio_num);
    if (removed != ESP_OK && removed != ESP_ERR_INVALID_STATE) return removed;
    /* INVALID_STATE means the GPIO ISR service is not installed: no handler. */
    tp->config.interrupt_callback = NULL;
    ESP_RETURN_ON_ERROR(gpio_reset_pin(tp->config.int_gpio_num), TAG,
                        "release interrupt pin");
    touch->interrupt_configured = false;
  }
  if (touch->reset_configured) {
    ESP_RETURN_ON_ERROR(gpio_reset_pin(tp->config.rst_gpio_num), TAG,
                        "release reset pin");
    touch->reset_configured = false;
  }
  heap_caps_free(touch);
  return ESP_OK;
}

esp_err_t esp_lcd_touch_new_i2c_axs5106(i2c_master_dev_handle_t device,
                                        const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *out_touch) {
  if (out_touch == NULL) return ESP_ERR_INVALID_ARG;
  *out_touch = NULL;
  if (device == NULL || config == NULL ||
      (config->rst_gpio_num != GPIO_NUM_NC &&
       !GPIO_IS_VALID_OUTPUT_GPIO(config->rst_gpio_num)) ||
      (config->int_gpio_num != GPIO_NUM_NC &&
       (!GPIO_IS_VALID_GPIO(config->int_gpio_num) ||
        config->int_gpio_num == config->rst_gpio_num)) ||
      (config->interrupt_callback != NULL &&
       config->int_gpio_num == GPIO_NUM_NC))
    return ESP_ERR_INVALID_ARG;
  axs5106_t *touch = heap_caps_calloc(1, sizeof(*touch), MALLOC_CAP_DEFAULT);
  if (touch == NULL) return ESP_ERR_NO_MEM;
  touch->device = device;
  touch->base.config = *config;
  touch->base.config.interrupt_callback = NULL;
  touch->base.data.lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
  touch->base.read_data = axs5106_read;
  touch->base.get_xy = axs5106_get_xy;
  touch->base.del = axs5106_delete;
  esp_err_t ret;
  if (config->rst_gpio_num != GPIO_NUM_NC) {
    const gpio_config_t gpio = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = UINT64_C(1) << config->rst_gpio_num,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&gpio), fail, TAG, "configure reset pin");
    touch->reset_configured = true;
    ESP_GOTO_ON_ERROR(
        gpio_set_level(config->rst_gpio_num, config->levels.reset), fail, TAG,
        "assert reset");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_GOTO_ON_ERROR(
        gpio_set_level(config->rst_gpio_num, !config->levels.reset), fail, TAG,
        "release reset");
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (config->int_gpio_num != GPIO_NUM_NC) {
    const gpio_config_t gpio = {
        .mode = GPIO_MODE_INPUT,
        .intr_type =
            config->levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE,
        .pin_bit_mask = UINT64_C(1) << config->int_gpio_num,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&gpio), fail, TAG, "configure interrupt pin");
    touch->interrupt_configured = true;
    ESP_GOTO_ON_ERROR(gpio_intr_disable(config->int_gpio_num), fail, TAG,
                      "disable interrupt until callback registration");
  }
  if (config->interrupt_callback != NULL) {
    /* Register last: no fallible initialization remains after an ISR can run.
     */
    ret = esp_lcd_touch_register_interrupt_callback(&touch->base,
                                                    config->interrupt_callback);
    if (ret != ESP_OK) {
      /* The pinned library only returns success after installing the handler.
       */
      touch->base.config.interrupt_callback = NULL;
      goto fail;
    }
  }
  *out_touch = &touch->base;
  return ESP_OK;
fail:
  /* No handler was installed. Cleanup cannot expose the discarded instance. */
  if (touch->interrupt_configured) {
    (void)gpio_intr_disable(config->int_gpio_num);
    (void)gpio_reset_pin(config->int_gpio_num);
  }
  if (touch->reset_configured) (void)gpio_reset_pin(config->rst_gpio_num);
  heap_caps_free(touch);
  return ret;
}
