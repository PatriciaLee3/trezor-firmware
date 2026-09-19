#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
typedef enum {
  LCD_RGB_ELEMENT_ORDER_RGB,
  LCD_RGB_ELEMENT_ORDER_BGR
} lcd_rgb_element_order_t;
typedef enum {
  LCD_RGB_DATA_ENDIAN_BIG,
  LCD_RGB_DATA_ENDIAN_LITTLE
} lcd_rgb_data_endian_t;
typedef struct {
  lcd_rgb_element_order_t rgb_ele_order;
  lcd_rgb_data_endian_t data_endian;
  uint32_t bits_per_pixel;
  gpio_num_t reset_gpio_num;
  void *vendor_config;
  struct {
    uint32_t reset_active_high : 1;
  } flags;
} esp_lcd_panel_dev_config_t;
