#pragma once
#include "esp_err.h"
#include "tusb.h"
enum { TINYUSB_EVENT_ATTACHED, TINYUSB_EVENT_DETACHED };
typedef struct { int id; uint8_t rhport; } tinyusb_event_t;
typedef struct { int size, priority, xCoreID; } tinyusb_task_config_t;
typedef struct {
  tinyusb_task_config_t task;
  struct {
    const tusb_desc_device_t *device;
    const uint8_t *full_speed_config;
    const char **string;
    int string_count;
  } descriptor;
  void (*event_cb)(tinyusb_event_t *, void *);
} tinyusb_config_t;
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config);
