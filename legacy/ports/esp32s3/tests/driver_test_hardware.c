#include "driver_test_hardware.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

test_gpio_t hw_gpio[49];
unsigned hw_calls[HW_OPERATIONS], hw_live_allocations;
uint32_t hw_delays[256];
unsigned hw_delay_count;
static bool service;
static int fail_operation = -1;
static unsigned fail_at;

void hw_assert_released(void) {
  assert(hw_live_allocations == 0);
  for (unsigned i = 0; i < 49; i++) {
    assert(!hw_gpio[i].configured && hw_gpio[i].handler == NULL);
  }
}
void hw_reset(void) {
  hw_assert_released();
  memset(hw_gpio, 0, sizeof(hw_gpio));
  memset(hw_calls, 0, sizeof(hw_calls));
  hw_delay_count = 0;
  service = false;
  fail_operation = -1;
}
void hw_fail(enum hw_operation operation, unsigned nth) {
  fail_operation = operation;
  fail_at = hw_calls[operation] + nth;
}
static esp_err_t operation(enum hw_operation op) {
  hw_calls[op]++;
  if ((int)op == fail_operation && hw_calls[op] == fail_at) {
    fail_operation = -1;
    return ESP_ERR_TIMEOUT;
  }
  return ESP_OK;
}
void *heap_caps_calloc(size_t count, size_t size, unsigned caps) {
  (void)caps;
  if (operation(HW_ALLOC) != ESP_OK) return NULL;
  void *p = calloc(count, size);
  assert(p != NULL);
  hw_live_allocations++;
  return p;
}
void heap_caps_free(void *ptr) {
  if (ptr == NULL) return;
  for (unsigned i = 0; i < 49; i++) assert(hw_gpio[i].arg != ptr);
  assert(hw_live_allocations > 0);
  hw_live_allocations--;
  free(ptr);
}
void vTaskDelay(uint32_t ticks) {
  assert(hw_delay_count < 256);
  hw_delays[hw_delay_count++] = ticks;
}
esp_err_t gpio_config(const gpio_config_t *config) {
  esp_err_t err = operation(HW_CONFIG);
  if (err != ESP_OK) return err;
  for (unsigned i = 0; i < 49; i++)
    if (config->pin_bit_mask & (UINT64_C(1) << i)) {
      hw_gpio[i].configured = true;
      hw_gpio[i].edge = config->intr_type;
      hw_gpio[i].enabled = config->intr_type != GPIO_INTR_DISABLE;
    }
  return ESP_OK;
}
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t value) {
  assert(GPIO_IS_VALID_GPIO(pin));
  esp_err_t err = operation(HW_LEVEL);
  if (err == ESP_OK) hw_gpio[pin].level = value;
  return err;
}
esp_err_t gpio_reset_pin(gpio_num_t pin) {
  assert(GPIO_IS_VALID_GPIO(pin));
  esp_err_t err = operation(HW_RESET);
  if (err == ESP_OK) {
    assert(hw_gpio[pin].handler == NULL);
    memset(&hw_gpio[pin], 0, sizeof(hw_gpio[pin]));
  }
  return err;
}
esp_err_t gpio_intr_disable(gpio_num_t pin) {
  assert(GPIO_IS_VALID_GPIO(pin));
  esp_err_t err = operation(HW_DISABLE);
  if (err == ESP_OK) hw_gpio[pin].enabled = false;
  return err;
}
esp_err_t gpio_intr_enable(gpio_num_t pin) {
  assert(GPIO_IS_VALID_GPIO(pin));
  esp_err_t err = operation(HW_ENABLE);
  if (err == ESP_OK) hw_gpio[pin].enabled = true;
  return err;
}
esp_err_t gpio_install_isr_service(int flags) {
  (void)flags;
  esp_err_t err = operation(HW_INSTALL);
  if (err != ESP_OK) return err;
  if (service) return ESP_ERR_INVALID_STATE;
  service = true;
  return ESP_OK;
}
esp_err_t gpio_isr_handler_add(gpio_num_t pin, gpio_isr_t handler, void *arg) {
  assert(service && GPIO_IS_VALID_GPIO(pin));
  esp_err_t err = operation(HW_ADD);
  if (err == ESP_OK) {
    hw_gpio[pin].handler = handler;
    hw_gpio[pin].arg = arg;
  }
  return err;
}
esp_err_t gpio_isr_handler_remove(gpio_num_t pin) {
  assert(GPIO_IS_VALID_GPIO(pin));
  esp_err_t err = operation(HW_REMOVE);
  if (err != ESP_OK) return err;
  if (!service) return ESP_ERR_INVALID_STATE;
  hw_gpio[pin].handler = NULL;
  hw_gpio[pin].arg = NULL;
  return ESP_OK;
}
