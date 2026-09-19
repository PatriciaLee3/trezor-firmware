#pragma once
#define TINYUSB_DEFAULT_CONFIG(cb) ((tinyusb_config_t){.event_cb = cb})
#define TINYUSB_TASK_CUSTOM(s, p, a) ((tinyusb_task_config_t){s, p, a})
