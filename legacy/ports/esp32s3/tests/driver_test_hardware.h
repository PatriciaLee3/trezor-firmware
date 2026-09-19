#pragma once
#include <stdbool.h>
#include "driver/gpio.h"
enum hw_operation {
  HW_ALLOC,
  HW_CONFIG,
  HW_LEVEL,
  HW_RESET,
  HW_DISABLE,
  HW_ENABLE,
  HW_INSTALL,
  HW_ADD,
  HW_REMOVE,
  HW_OPERATIONS
};
typedef struct {
  bool configured, enabled;
  int level, edge;
  gpio_isr_t handler;
  void *arg;
} test_gpio_t;
extern test_gpio_t hw_gpio[49];
extern unsigned hw_calls[HW_OPERATIONS], hw_live_allocations;
extern uint32_t hw_delays[256];
extern unsigned hw_delay_count;
void hw_reset(void);
void hw_fail(enum hw_operation operation, unsigned nth);
void hw_assert_released(void);
