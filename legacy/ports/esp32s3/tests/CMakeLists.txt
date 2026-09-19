cmake_minimum_required(VERSION 3.22)
project(esp32s3_port_tests C)
enable_testing()
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(PORT "${CMAKE_CURRENT_LIST_DIR}/..")
set(TUSB "${PORT}/managed_components/espressif__tinyusb/src")
add_compile_definitions(EMULATOR=1 ESP32S3=1 U2F_ENABLED=1 DEBUG_LINK=0
  DEBUG_LOG=0 BITCOIN_ONLY=0 FLASH_BLOCK_WORDS=1 FLASH_BIT_ACCESS=1)
add_compile_options(-UNDEBUG -Wall -Wextra -Werror -Wno-unused-parameter
  -Wno-unused-function -Wno-unused-variable -Wno-sign-compare)
include_directories(stubs "${PORT}/main" "${PORT}/main/compat"
  "${PORT}/boards" "${REPO}/legacy/firmware/protob" "${REPO}/legacy"
  "${REPO}/legacy/firmware" "${REPO}/crypto" "${REPO}/storage"
  "${REPO}/vendor/nanopb" "${TUSB}")
add_executable(test_logic test_logic.c "${PORT}/main/port_logic.c")
add_test(NAME touch_and_packet_ring COMMAND test_logic)
# Test the real drivers and pinned touch framework; only hardware is stubbed.
set(AXS5106_SOURCE "${PORT}/components/esp_lcd_touch_axs5106/esp_lcd_touch_axs5106.c")
set(TOUCH_FRAMEWORK "${PORT}/managed_components/espressif__esp_lcd_touch")
foreach(CAPACITY IN ITEMS 2 1)
  set(TOUCH_TEST test_touch_io)
  if(CAPACITY EQUAL 1)
    set(TOUCH_TEST test_touch_capacity_one)
  endif()
  add_executable(${TOUCH_TEST} test_touch_io.c driver_test_hardware.c
    "${PORT}/main/port_logic.c" "${AXS5106_SOURCE}"
    "${TOUCH_FRAMEWORK}/esp_lcd_touch.c")
  target_include_directories(${TOUCH_TEST} BEFORE PRIVATE driver_stubs
    "${PORT}/components/esp_lcd_touch_axs5106/include"
    "${TOUCH_FRAMEWORK}/include")
  target_compile_definitions(${TOUCH_TEST} PRIVATE CONFIG_ESP_LCD_TOUCH_MAX_POINTS=${CAPACITY})
endforeach()
add_test(NAME touch_driver_i2c_fail_closed COMMAND test_touch_io)
add_test(NAME touch_driver_capacity_one COMMAND test_touch_capacity_one)

add_executable(test_lcd test_lcd.c driver_test_hardware.c
  "${PORT}/components/esp_lcd_jd9853/esp_lcd_jd9853.c")
target_include_directories(test_lcd BEFORE PRIVATE driver_stubs
  "${PORT}/components/esp_lcd_jd9853/include")
add_test(NAME lcd_driver_protocol_and_failures COMMAND test_lcd)
add_executable(test_usb test_usb.c "${PORT}/main/port_logic.c"
  "${REPO}/legacy/firmware/usb.c" "${REPO}/legacy/usb21_standard.c"
  "${REPO}/legacy/webusb.c" "${REPO}/legacy/winusb.c")
add_test(NAME usb_descriptors_and_backpressure COMMAND test_usb)
add_executable(test_storage test_storage.c "${REPO}/storage/flash_area.c"
  "${PORT}/main/esp32s3_norcow.c"
  "${REPO}/crypto/sha2.c" "${REPO}/storage/storage_utils.c"
  "${REPO}/crypto/memzero.c")
add_test(NAME flash_bounds_and_salt COMMAND test_storage --basic)
add_test(NAME norcow_compaction_power_loss COMMAND test_storage)
add_executable(test_storage_upgrade test_storage.c "${REPO}/storage/flash_area.c"
  "${PORT}/main/esp32s3_norcow.c"
  "${REPO}/crypto/sha2.c" "${REPO}/storage/storage_utils.c"
  "${REPO}/crypto/memzero.c")
target_compile_definitions(test_storage_upgrade PRIVATE TEST_NORCOW_UPGRADE=1)
target_compile_options(test_storage_upgrade PRIVATE
  "-include${CMAKE_CURRENT_LIST_DIR}/upgrade_config.h")
add_test(NAME norcow_future_upgrade_power_loss COMMAND test_storage_upgrade)
