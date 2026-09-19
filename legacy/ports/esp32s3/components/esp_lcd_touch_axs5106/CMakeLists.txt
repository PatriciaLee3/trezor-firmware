idf_component_register(
  SRCS "esp_lcd_touch_axs5106.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_driver_i2c espressif__esp_lcd_touch
  PRIV_REQUIRES esp_driver_gpio esp_rom heap freertos)
