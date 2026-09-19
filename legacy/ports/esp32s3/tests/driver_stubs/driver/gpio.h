#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
typedef void (*gpio_isr_t)(void *);
#define GPIO_NUM_NC (-1)
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_POSEDGE 1
#define GPIO_INTR_NEGEDGE 2
#define GPIO_IS_VALID_GPIO(pin) ((unsigned)(pin) < 49)
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) GPIO_IS_VALID_GPIO(pin)
typedef struct {
  int mode, intr_type;
  uint64_t pin_bit_mask;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t value);
esp_err_t gpio_reset_pin(gpio_num_t pin);
esp_err_t gpio_intr_disable(gpio_num_t pin);
esp_err_t gpio_intr_enable(gpio_num_t pin);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(gpio_num_t pin, gpio_isr_t handler, void *arg);
esp_err_t gpio_isr_handler_remove(gpio_num_t pin);
