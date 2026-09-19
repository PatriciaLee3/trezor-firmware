#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "driver_test_hardware.h"
#include "esp_lcd_touch_axs5106.h"
#include "port_error.h"
#include "port_logic.h"
#include "waveshare_touch_lcd_1_47.h"

struct test_i2c_device {
  uint8_t report[14];
  esp_err_t tx_result, rx_result;
  unsigned tx_calls, rx_calls;
};
static struct test_i2c_device device;
static esp_lcd_touch_handle_t touch;
static port_touch_t gesture;
static unsigned decoded, failures;
static bool expect_failure;
static jmp_buf stopped;

void esp32s3_port_error(const char *message, const char *file, int line) {
  assert(expect_failure && message != NULL && file != NULL && line > 0);
  failures++;
  longjmp(stopped, 1);
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t handle,
                              const uint8_t *data, size_t size,
                              int timeout_ms) {
  assert(handle != NULL && size == 1 && data[0] == 1 && timeout_ms == 100);
  handle->tx_calls++;
  return handle->tx_result;
}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t handle, uint8_t *data,
                             size_t size, int timeout_ms) {
  assert(handle != NULL && size == sizeof(handle->report) && timeout_ms == 100);
  handle->rx_calls++;
  /* Failed RX may still overwrite the entire destination with plausible data.
   */
  memcpy(data, handle->report, size);
  return handle->rx_result;
}

static void set_report(struct test_i2c_device *dev, uint8_t count, uint16_t x,
                       uint16_t y) {
  memset(dev->report, 0, sizeof(dev->report));
  dev->report[1] = count;
  dev->report[2] = (x >> 8) | 0x80;
  dev->report[3] = x;
  dev->report[4] = (y >> 8) | 0x40;
  dev->report[5] = y;
  dev->report[8] = 0xa1;
  dev->report[9] = 0x23;
  dev->report[10] = 0xb2;
  dev->report[11] = 0x34;
}
static const esp_lcd_touch_config_t board_config = {
    .x_max = BOARD_PANEL_NATIVE_WIDTH,
    .y_max = BOARD_PANEL_NATIVE_HEIGHT,
    .rst_gpio_num = BOARD_TOUCH_RST,
    .int_gpio_num = BOARD_TOUCH_INT,
};
static const esp_lcd_touch_config_t no_pins = {
    .rst_gpio_num = GPIO_NUM_NC,
    .int_gpio_num = GPIO_NUM_NC,
};

static port_touch_event_t sample(uint32_t now) {
  /* Use the real public framework, then the board's transform and gestures. */
  PORT_CHECK(esp_lcd_touch_read_data(touch));
  uint16_t x[2] = {0}, y[2] = {0};
  uint8_t count = 0;
  (void)esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 2);
  decoded++;
  uint16_t sx = UINT16_MAX, sy = UINT16_MAX;
  if (count == 1) (void)port_touch_rotate(x[0], y[0], &sx, &sy);
  return port_touch_update(&gesture, count, sx, sy, now);
}
static void begin_confirm(void) {
  memset(&gesture, 0, sizeof(gesture));
  device.tx_result = device.rx_result = ESP_OK;
  set_report(&device, 1, 130, 250);
  assert(sample(100).down == PORT_ZONE_NONE);
  assert(sample(115).down == PORT_ZONE_CONFIRM);
}
static void fault_case(esp_err_t tx, esp_err_t rx) {
  begin_confirm();
  const unsigned before = decoded, previous_failures = failures;
  device.tx_calls = device.rx_calls = 0;
  device.tx_result = tx;
  device.rx_result = rx;
  set_report(&device, 0, 130, 250);
  expect_failure = true;
  if (setjmp(stopped) == 0) {
    (void)sample(120);
    assert(!"failed I2C sample returned to the wallet");
  }
  expect_failure = false;
  assert(failures == previous_failures + 1 && decoded == before);
  assert(device.tx_calls == 1 && device.rx_calls == (tx == ESP_OK ? 1u : 0u));
  assert(gesture.state == PORT_TOUCH_ACTIVE);
  assert(touch->data.points == 0);
  /* Also verify the original transport error at the driver boundary. */
  assert(esp_lcd_touch_read_data(touch) == (tx != ESP_OK ? tx : rx));
}
static void test_gestures(void) {
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &board_config, &touch) ==
         ESP_OK);
  begin_confirm();
  set_report(&device, 0, 0, 0);
  assert(sample(120).released == PORT_ZONE_CONFIRM);
  assert(sample(121).released == PORT_ZONE_NONE);
  begin_confirm();
  set_report(&device, 2, 130, 250);
  assert(sample(120).down == PORT_ZONE_NONE);
  set_report(&device, 0, 0, 0);
  assert(sample(121).released == PORT_ZONE_NONE);
  const esp_err_t errors[] = {ESP_FAIL, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_STATE};
  for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
    fault_case(errors[i], ESP_OK);
    fault_case(ESP_OK, errors[i]);
    fault_case(errors[i], errors[i]);
  }
  assert(failures == 9);
  assert(esp_lcd_touch_del(touch) == ESP_OK);
  memset(&device, 0, sizeof(device));
  hw_assert_released();
}

static void test_samples(void) {
  hw_reset();
  struct test_i2c_device other = {0};
  esp_lcd_touch_handle_t second;
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &no_pins, &touch) == ESP_OK);
  assert(esp_lcd_touch_new_i2c_axs5106(&other, &no_pins, &second) == ESP_OK);
  set_report(&device, 2, 0xabc, 0xdef);
  set_report(&other, 1, 23, 45);
  assert(esp_lcd_touch_read_data(touch) == ESP_OK);
  assert(esp_lcd_touch_read_data(second) == ESP_OK);
  assert(device.tx_calls > 0 && other.tx_calls == 1 && other.rx_calls == 1);
  uint16_t x[3] = {0, 0, 0xdead}, y[3] = {0, 0, 0xbeef};
  uint16_t strength[3] = {99, 99, 0xabcd};
  uint8_t count;
  assert(esp_lcd_touch_get_coordinates(touch, x, y, strength, &count, 2));
  assert(count == CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
  assert(x[0] == 0xabc && y[0] == 0xdef && strength[0] == 0);
  if (CONFIG_ESP_LCD_TOUCH_MAX_POINTS == 2)
    assert(x[1] == 0x123 && y[1] == 0x234 && strength[1] == 0);
  assert(x[2] == 0xdead && y[2] == 0xbeef && strength[2] == 0xabcd);
  assert(!esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 2));
  assert(count == 0);
  assert(esp_lcd_touch_get_coordinates(second, x, y, NULL, &count, 2));
  assert(count == 1 && x[0] == 23 && y[0] == 45);
  assert(esp_lcd_touch_read_data(touch) == ESP_OK);
  x[1] = 0xaaaa;
  assert(esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 1));
  assert(count == 1 && x[1] == 0xaaaa);
  assert(!esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 2));

  /* A zero-point report must replace an unread earlier sample. */
  assert(esp_lcd_touch_read_data(touch) == ESP_OK);
  set_report(&device, 0, 0, 0);
  assert(esp_lcd_touch_read_data(touch) == ESP_OK);
  assert(!esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 2));
  for (uint8_t invalid = 3; invalid < 16; invalid++) {
    set_report(&device, 1, 130, 250);
    assert(esp_lcd_touch_read_data(touch) == ESP_OK);
    set_report(&device, invalid, 130, 250);
    assert(esp_lcd_touch_read_data(touch) == ESP_ERR_INVALID_RESPONSE);
    assert(!esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 2));
  }
  set_report(&device, 1, 130, 250);
  assert(esp_lcd_touch_read_data(touch) == ESP_OK);
  device.rx_result = ESP_FAIL;
  assert(esp_lcd_touch_read_data(touch) == ESP_FAIL);
  assert(!esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 2));
  device.rx_result = ESP_OK;
  assert(esp_lcd_touch_del(touch) == ESP_OK);
  /* The surviving device must still use its own transport and cache. */
  assert(esp_lcd_touch_read_data(second) == ESP_OK);
  assert(esp_lcd_touch_get_coordinates(second, x, y, NULL, &count, 2));
  assert(x[0] == 23 && y[0] == 45);
  assert(esp_lcd_touch_del(second) == ESP_OK);
  hw_assert_released();
}

static unsigned irq_calls;
static void interrupt_callback(esp_lcd_touch_handle_t tp) {
  assert(tp != NULL && tp->config.user_data == &irq_calls);
  irq_calls++;
}
static void test_lifecycle(void) {
  esp_lcd_touch_config_t config = board_config;
  config.interrupt_callback = interrupt_callback;
  config.user_data = &irq_calls;
  const enum hw_operation ops[] = {HW_ALLOC,   HW_CONFIG, HW_LEVEL, HW_DISABLE,
                                   HW_INSTALL, HW_ENABLE, HW_ADD};
  for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
    for (unsigned nth = 1;
         nth <= (ops[i] == HW_CONFIG || ops[i] == HW_LEVEL ? 2u : 1u); nth++) {
      hw_reset();
      hw_fail(ops[i], nth);
      touch = (esp_lcd_touch_handle_t)(uintptr_t)1;
      const esp_err_t expected =
          ops[i] == HW_ALLOC ? ESP_ERR_NO_MEM : ESP_ERR_TIMEOUT;
      assert(esp_lcd_touch_new_i2c_axs5106(&device, &config, &touch) ==
             expected);
      assert(touch == NULL);
      hw_assert_released();
    }
  }
  hw_reset();
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &config, &touch) == ESP_OK);
  assert(hw_gpio[BOARD_TOUCH_INT].enabled);
  hw_gpio[BOARD_TOUCH_INT].handler(hw_gpio[BOARD_TOUCH_INT].arg);
  assert(irq_calls == 1);
  assert(hw_delay_count == 2 && hw_delays[0] == 10 && hw_delays[1] == 10);
  for (unsigned i = 0; i < 3; i++) {
    const enum hw_operation failures[] = {HW_DISABLE, HW_REMOVE, HW_RESET};
    hw_fail(failures[i], 1);
    assert(esp_lcd_touch_del(touch) == ESP_ERR_TIMEOUT);
    assert(hw_live_allocations == 1);
  }
  assert(esp_lcd_touch_del(touch) == ESP_OK);
  hw_assert_released();

  /* Public callback registration after construction still works. */
  hw_reset();
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &board_config, &touch) ==
         ESP_OK);
  assert(!hw_gpio[BOARD_TOUCH_INT].enabled);
  assert(hw_gpio[BOARD_TOUCH_INT].edge == GPIO_INTR_NEGEDGE);
  assert(esp_lcd_touch_register_interrupt_callback_with_data(
             touch, interrupt_callback, &irq_calls) == ESP_OK);
  hw_gpio[BOARD_TOUCH_INT].handler(hw_gpio[BOARD_TOUCH_INT].arg);
  assert(irq_calls == 2);
  /* Failed public unregister clears config before returning its error.
   * Deletion must still remove the live handler before freeing the object. */
  hw_fail(HW_REMOVE, 1);
  assert(esp_lcd_touch_register_interrupt_callback(touch, NULL) ==
         ESP_ERR_TIMEOUT);
  assert(touch->config.interrupt_callback == NULL);
  assert(hw_gpio[BOARD_TOUCH_INT].handler != NULL);
  assert(esp_lcd_touch_del(touch) == ESP_OK);
  hw_assert_released();

  hw_reset();
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &board_config, &touch) ==
         ESP_OK);
  assert(esp_lcd_touch_register_interrupt_callback_with_data(
             touch, interrupt_callback, &irq_calls) == ESP_OK);
  esp_lcd_touch_config_t other_config = no_pins;
  other_config.int_gpio_num = 5;
  other_config.interrupt_callback = interrupt_callback;
  other_config.user_data = &irq_calls;
  esp_lcd_touch_handle_t second;
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &other_config, &second) ==
         ESP_OK);
  assert(esp_lcd_touch_del(touch) == ESP_OK);
  assert(hw_gpio[5].handler != NULL && hw_gpio[5].enabled);
  hw_gpio[5].handler(hw_gpio[5].arg);
  assert(irq_calls == 3);
  assert(esp_lcd_touch_register_interrupt_callback(second, NULL) == ESP_OK);
  assert(esp_lcd_touch_del(second) == ESP_OK);
  hw_assert_released();

  hw_reset();
  assert(esp_lcd_touch_new_i2c_axs5106(NULL, &config, &touch) ==
         ESP_ERR_INVALID_ARG);
  assert(touch == NULL);
  assert(esp_lcd_touch_new_i2c_axs5106(&device, NULL, &touch) ==
         ESP_ERR_INVALID_ARG);
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &config, NULL) ==
         ESP_ERR_INVALID_ARG);
  config.rst_gpio_num = 64;
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &config, &touch) ==
         ESP_ERR_INVALID_ARG);
  config = board_config;
  config.int_gpio_num = config.rst_gpio_num;
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &config, &touch) ==
         ESP_ERR_INVALID_ARG);
  config = no_pins;
  config.interrupt_callback = interrupt_callback;
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &config, &touch) ==
         ESP_ERR_INVALID_ARG);
  hw_assert_released();
}

static void adjust_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x,
                               uint16_t *y, uint16_t *strength, uint8_t *count,
                               uint8_t max) {
  assert(tp->config.driver_data == &device && *count == 1 && max == 1);
  x[0]++;
}
static void test_framework(void) {
  hw_reset();
  esp_lcd_touch_config_t config = no_pins;
  config.x_max = 172;
  config.y_max = 320;
  config.flags.mirror_x = 1;
  config.flags.swap_xy = 1;
  config.process_coordinates = adjust_coordinates;
  config.driver_data = &device;
  assert(esp_lcd_touch_new_i2c_axs5106(&device, &config, &touch) == ESP_OK);
  set_report(&device, 1, 10, 20);
  assert(esp_lcd_touch_read_data(touch) == ESP_OK);
  uint16_t x, y;
  uint8_t count;
  assert(esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &count, 1));
  assert(count == 1 && x == 20 && y == 161);
  assert(esp_lcd_touch_del(touch) == ESP_OK);
  hw_assert_released();
}

int main(void) {
  if (CONFIG_ESP_LCD_TOUCH_MAX_POINTS >= 2) test_gestures();
  test_samples();
  test_lifecycle();
  test_framework();
  puts(
      "AXS5106: transport faults, gestures, reports, instances and lifecycle "
      "passed");
  return 0;
}
