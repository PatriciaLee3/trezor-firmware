idf_component_register(
  SRCS "esp_lcd_jd9853.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_lcd
  PRIV_REQUIRES esp_driver_gpio heap freertos)
