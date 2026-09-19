/* TinyUSB application class driver: owns OUT re-arming for nonblocking NAK
 * backpressure. No Legacy callback is invoked from the USB task. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "device/usbd_pvt.h"
#include "common.h"
#include "port_logic.h"
#include <libopencm3/usb/usbd.h>

#define MAX_CONTROLS 12
#define MAX_CONFIGS 8
#define DESCRIPTOR_CAPACITY 256
#define CACHE_COUNT 6
typedef struct {
  uint8_t type, mask;
  usbd_control_callback callback;
} control_entry_t;
struct usbd_device {
  const struct usb_device_descriptor *device;
  const struct usb_config_descriptor *config;
  const char *const *strings;
  int string_count;
  uint8_t *control_buffer;
  uint16_t control_buffer_size;
  usbd_endpoint_callback callbacks[16];
  control_entry_t controls[MAX_CONTROLS];
  size_t control_count;
  usbd_set_config_callback configs[MAX_CONFIGS];
  size_t config_count;
  bool installed;
};
typedef struct {
  struct usb_setup_data request;
  uint16_t size;
  uint8_t data[DESCRIPTOR_CAPACITY] __attribute__((aligned(4)));
} cached_descriptor_t;
typedef struct {
  port_packet_ring_t ring;
  uint8_t out[64] __attribute__((aligned(4)));
  uint8_t in[64] __attribute__((aligned(4)));
  bool opened, out_armed, in_busy, in_pending;
} channel_t;

const struct _usbd_driver otgfs_usb_driver = {0};
static usbd_device singleton;
static uint8_t device_descriptor[18] DRAM_ATTR;
static uint8_t config_descriptor[DESCRIPTOR_CAPACITY] DRAM_ATTR;
static cached_descriptor_t cache[CACHE_COUNT] DRAM_ATTR;
static const uint8_t *hid_descriptor;
static const char *tinyusb_strings[7];
static const uint16_t language_id = 0x0409;
static channel_t channels[2] DRAM_ATTR;
static portMUX_TYPE channel_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t generation;
/* Owned by CPU0, saved/restored across recursive Legacy polls. */
static const uint8_t *active_packet;
static void *active_read_buffer;
static uint8_t active_endpoint;
static uint32_t active_generation;
static bool poll_u2f_next;
static uint8_t hid_idle;
static bool io_scheduled;
static volatile bool usb_fault;

static void schedule_io(void);

static uint8_t out_ep(unsigned i) { return i == 0 ? 1 : 3; }
static int ep_channel(uint8_t ep) {
  return (ep & 0x7f) == 1 ? 0 : (ep & 0x7f) == 3 ? 1 : -1;
}
static bool append(uint16_t *offset, const void *bytes, uint16_t size) {
  if (size > DESCRIPTOR_CAPACITY - *offset || (size != 0 && bytes == NULL))
    return false;
  if (size != 0) memcpy(config_descriptor + *offset, bytes, size);
  *offset += size;
  return true;
}
static void serialize_descriptors(void) {
  ensure(sectrue * (singleton.config->bNumInterfaces == 2), "USB interfaces");
  memcpy(device_descriptor, singleton.device, sizeof(device_descriptor));
  uint16_t offset = 0;
  ensure(sectrue * append(&offset, singleton.config, 9), "USB config");
  for (unsigned i = 0; i < singleton.config->bNumInterfaces; i++) {
    const struct usb_interface *itf = &singleton.config->interface[i];
    for (unsigned a = 0; a < itf->num_altsetting; a++) {
      const struct usb_interface_descriptor *d = &itf->altsetting[a];
      ensure(sectrue * append(&offset, d, 9), "USB interface");
      ensure(sectrue * append(&offset, d->extra, d->extralen), "USB extra");
      for (unsigned e = 0; e < d->bNumEndpoints; e++)
        ensure(sectrue * append(&offset, &d->endpoint[e], 7), "USB endpoint");
    }
  }
  config_descriptor[2] = offset;
  config_descriptor[3] = offset >> 8;
}
static void cache_request(unsigned index, uint8_t type, uint8_t request,
                          uint16_t value, uint16_t interface) {
  cached_descriptor_t *entry = &cache[index];
  entry->request = (struct usb_setup_data){type, request, value, interface, 256};
  uint8_t *buffer = singleton.control_buffer;
  uint16_t length = singleton.control_buffer_size;
  usbd_control_complete_callback complete = NULL;
  for (size_t i = 0; i < singleton.control_count; i++) {
    const control_entry_t *cb = &singleton.controls[i];
    if ((type & cb->mask) != cb->type) continue;
    int result = cb->callback(&singleton, &entry->request, &buffer, &length, &complete);
    if (result == USBD_REQ_NEXT_CALLBACK) continue;
    ensure(sectrue * (result == USBD_REQ_HANDLED && complete == NULL &&
                      length > 0 && length <= sizeof(entry->data)), "USB cache");
    memcpy(entry->data, buffer, length);
    entry->size = length;
    return;
  }
  __fatal_error("USB descriptor missing", __FILE__, __LINE__);
}
static void class_reset(uint8_t rhport) {
  (void)rhport;
  portENTER_CRITICAL(&channel_lock);
  generation++;
  port_clear(channels, sizeof(channels));
  portEXIT_CRITICAL(&channel_lock);
  hid_idle = 0;
}
static void device_event(tinyusb_event_t *event, void *arg) {
  (void)arg;
  if (event->id == TINYUSB_EVENT_DETACHED) class_reset(event->rhport);
}
static void install_tinyusb(void) {
  if (singleton.installed) return;
  for (size_t i = 0; i < singleton.config_count; i++)
    singleton.configs[i](&singleton, singleton.config->bConfigurationValue);
  serialize_descriptors();
  cache_request(0, 0x80, 6, 0x0f00, 0); /* BOS */
  cache_request(1, 0x81, 6, 0x2200, 1); /* U2F report */
  cache_request(2, 0xc0, 1, 1, 2);      /* WebUSB URL */
  cache_request(3, 0x80, 6, 0x03ee, 0); /* Microsoft OS string */
  cache_request(4, 0xc0, 0x21, 0, 4);   /* WCID */
  cache_request(5, 0xc1, 0x21, 0, 5);   /* DeviceInterfaceGUIDs */
  tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG(device_event);
  config.task = TINYUSB_TASK_CUSTOM(8192, 5, 1);
  config.descriptor.device = (const tusb_desc_device_t *)device_descriptor;
  config.descriptor.full_speed_config = config_descriptor;
  tinyusb_strings[0] = (const char *)&language_id;
  for (int i = 0; i < singleton.string_count && i + 1 < 7; i++)
    tinyusb_strings[i + 1] = singleton.strings[i];
  config.descriptor.string = tinyusb_strings;
  config.descriptor.string_count = singleton.string_count + 1;
  ensure(sectrue * (tinyusb_driver_install(&config) == ESP_OK), "USB install");
  singleton.installed = true;
}
usbd_device *usbd_init(const struct _usbd_driver *driver,
                       const struct usb_device_descriptor *device,
                       const struct usb_config_descriptor *config,
                       const char *const *strings, int string_count,
                       uint8_t *buffer, uint16_t size) {
  (void)driver;
  ensure(sectrue * !singleton.installed, "USB already initialized");
  singleton.device = device;
  singleton.config = config;
  singleton.strings = strings;
  singleton.string_count = string_count;
  singleton.control_buffer = buffer;
  singleton.control_buffer_size = size;
  return &singleton;
}
void usbd_register_set_config_callback(usbd_device *device,
                                       usbd_set_config_callback callback) {
  for (size_t i = 0; i < device->config_count; i++)
    if (device->configs[i] == callback) return;
  ensure(sectrue * (device->config_count < MAX_CONFIGS), "USB callbacks");
  device->configs[device->config_count++] = callback;
}
void usbd_register_control_callback(usbd_device *device, uint8_t type,
                                    uint8_t mask, usbd_control_callback callback) {
  for (size_t i = 0; i < device->control_count; i++) {
    const control_entry_t *entry = &device->controls[i];
    if (entry->type == type && entry->mask == mask && entry->callback == callback) return;
  }
  ensure(sectrue * (device->control_count < MAX_CONTROLS), "USB controls");
  device->controls[device->control_count++] = (control_entry_t){type, mask, callback};
}
void usbd_ep_setup(usbd_device *device, uint8_t address, uint8_t type,
                   uint16_t size, usbd_endpoint_callback callback) {
  ensure(sectrue * (ep_channel(address) >= 0 && type == 3 && size == 64), "USB endpoint");
  if ((address & 0x80) == 0) device->callbacks[address] = callback;
}
static void arm_out(unsigned i) {
  channel_t *channel = &channels[i];
  portENTER_CRITICAL(&channel_lock);
  bool arm = channel->opened && !channel->out_armed && channel->ring.count < 8;
  if (arm) channel->out_armed = true;
  portEXIT_CRITICAL(&channel_lock);
  if (arm && (!usbd_edpt_claim(0, out_ep(i)) ||
              !usbd_edpt_xfer(0, out_ep(i), channel->out, 64, false))) {
    portENTER_CRITICAL(&channel_lock);
    channel->out_armed = false;
    portEXIT_CRITICAL(&channel_lock);
  }
}
/* Endpoint state transitions happen only on CPU1, serialized with bus reset. */
static void service_io(void *unused) {
  (void)unused;
  portENTER_CRITICAL(&channel_lock);
  io_scheduled = false;
  portEXIT_CRITICAL(&channel_lock);
  for (unsigned i = 0; i < 2; i++) {
    arm_out(i);
    channel_t *channel = &channels[i];
    portENTER_CRITICAL(&channel_lock);
    bool pending = channel->opened && channel->in_pending;
    if (pending) channel->in_pending = false;
    portEXIT_CRITICAL(&channel_lock);
    if (pending && (!usbd_edpt_claim(0, out_ep(i) | 0x80) ||
        !usbd_edpt_xfer(0, out_ep(i) | 0x80, channel->in, 64, false))) {
      portENTER_CRITICAL(&channel_lock);
      channel->in_pending = true;
      portEXIT_CRITICAL(&channel_lock);
    }
  }
}
static void schedule_io(void) {
  portENTER_CRITICAL(&channel_lock);
  bool schedule = !io_scheduled;
  io_scheduled = true;
  portEXIT_CRITICAL(&channel_lock);
  if (schedule) usbd_defer_func(service_io, NULL, false);
}
static bool receive_one(unsigned i) {
  uint8_t packet[64];
  portENTER_CRITICAL(&channel_lock);
  bool received = port_packet_pop(&channels[i].ring, packet);
  uint32_t packet_generation = generation;
  portEXIT_CRITICAL(&channel_lock);
  if (!received) return false;
  schedule_io();
  const uint8_t *saved_packet = active_packet;
  void *saved_read_buffer = active_read_buffer;
  uint8_t saved_endpoint = active_endpoint;
  uint32_t saved_generation = active_generation;
  active_packet = packet;
  active_read_buffer = NULL;
  active_endpoint = out_ep(i);
  active_generation = packet_generation;
  usbd_endpoint_callback callback = singleton.callbacks[active_endpoint];
  if (callback != NULL) callback(&singleton, active_endpoint);
  if (active_read_buffer != NULL) port_clear(active_read_buffer, 64);
  port_clear(packet, sizeof(packet));
  active_packet = saved_packet;
  active_read_buffer = saved_read_buffer;
  active_endpoint = saved_endpoint;
  active_generation = saved_generation;
  return true;
}
void usbd_poll(usbd_device *device) {
  (void)device;
  install_tinyusb();
  ensure(sectrue * !usb_fault, "USB receive invariant");
  if (tud_mounted()) schedule_io();
  unsigned first = poll_u2f_next ? 1 : 0;
  if (receive_one(first) || receive_one(first ^ 1)) poll_u2f_next = !poll_u2f_next;
  /* Let CPU0's idle task run; taskYIELD alone would starve its watchdog. */
  vTaskDelay(1);
}
uint16_t usbd_ep_read_packet(usbd_device *device, uint8_t address, void *buffer,
                             uint16_t length) {
  (void)device;
  if (active_packet == NULL || address != active_endpoint || buffer == NULL ||
      length < 64) return 0;
  portENTER_CRITICAL(&channel_lock);
  bool current = active_generation == generation;
  if (current) memcpy(buffer, active_packet, 64);
  portEXIT_CRITICAL(&channel_lock);
  if (current) active_read_buffer = buffer;
  return current ? 64 : 0;
}
uint16_t usbd_ep_write_packet(usbd_device *device, uint8_t address,
                              const void *buffer, uint16_t length) {
  (void)device;
  int i = ep_channel(address);
  if (!singleton.installed || i < 0 || (address & 0x80) == 0 ||
      buffer == NULL || length != 64 || !tud_mounted()) {
    vTaskDelay(1);
    return 0;
  }
  channel_t *channel = &channels[i];
  ensure(sectrue * !usb_fault, "USB receive invariant");
  portENTER_CRITICAL(&channel_lock);
  bool available = channel->opened && !channel->in_busy;
  if (available) {
    memcpy(channel->in, buffer, 64);
    channel->in_busy = true;
    channel->in_pending = true;
  }
  portEXIT_CRITICAL(&channel_lock);
  if (!available) { vTaskDelay(1); return 0; }
  schedule_io();
  return 64;
}
static void reset_deferred(void *unused) { (void)unused; class_reset(0); }
void usbd_disconnect(usbd_device *device, bool disconnected) {
  if (device == NULL || !device->installed) return;
  if (disconnected) { tud_disconnect(); usbd_defer_func(reset_deferred, NULL, false); }
  else tud_connect();
}
void esp32s3_usb_disconnect(void) { usbd_disconnect(&singleton, true); }
static void class_init(void) { class_reset(0); }
static bool class_deinit(void) { class_reset(0); return true; }
static uint16_t class_open(uint8_t rhport, const tusb_desc_interface_t *itf,
                            uint16_t max_length) {
  unsigned i = itf->bInterfaceNumber;
  if (i > 1 || itf->bInterfaceClass != (i == 0 ? 0xff : 3) ||
      itf->bAlternateSetting != 0 || itf->bNumEndpoints != 2) return 0;
  uint16_t length = 9 + (i == 1 ? 9 : 0) + 14;
  if (max_length < length) return 0;
  const uint8_t *next = (const uint8_t *)itf + 9;
  if (i == 1) { hid_descriptor = next; next += 9; }
  for (unsigned e = 0; e < 2; e++, next += 7) {
    const tusb_desc_endpoint_t *ep = (const tusb_desc_endpoint_t *)next;
    if (ep->bLength != 7 || ep->bDescriptorType != TUSB_DESC_ENDPOINT ||
        (ep->bEndpointAddress & 0x7f) != out_ep(i) || ep->wMaxPacketSize != 64 ||
        ep->bmAttributes.xfer != TUSB_XFER_INTERRUPT || !usbd_edpt_open(rhport, ep))
      return 0;
  }
  portENTER_CRITICAL(&channel_lock);
  channels[i].opened = true;
  portEXIT_CRITICAL(&channel_lock);
  return length;
}
static bool class_xfer(uint8_t rhport, uint8_t ep, xfer_result_t result,
                       uint32_t length) {
  (void)rhport;
  int i = ep_channel(ep);
  if (i < 0) return false;
  channel_t *channel = &channels[i];
  bool valid = true;
  portENTER_CRITICAL(&channel_lock);
  if ((ep & 0x80) != 0) {
    port_clear(channel->in, sizeof(channel->in));
    channel->in_busy = false;
  } else {
    if (result == XFER_RESULT_SUCCESS && length == 64 && channel->opened)
      valid = port_packet_push(&channel->ring, channel->out);
    port_clear(channel->out, sizeof(channel->out));
    channel->out_armed = false;
  }
  portEXIT_CRITICAL(&channel_lock);
  if (!valid) { usb_fault = true; tud_disconnect(); return false; }
  if ((ep & 0x80) == 0) arm_out(i);
  return true;
}
static bool cached_control(uint8_t rhport, uint8_t stage,
                            const tusb_control_request_t *request) {
  if (stage != CONTROL_STAGE_SETUP) return true;
  for (unsigned i = 0; i < CACHE_COUNT; i++) {
    const struct usb_setup_data *r = &cache[i].request;
    if (request->bmRequestType == r->bmRequestType &&
        request->bRequest == r->bRequest && request->wValue == r->wValue &&
        request->wIndex == r->wIndex)
      return tud_control_xfer(rhport, request, cache[i].data, cache[i].size);
  }
  if (request->bmRequestType == 0x81 && request->bRequest == 6 &&
      request->wValue == 0x2100 && request->wIndex == 1 && hid_descriptor != NULL)
    return tud_control_xfer(rhport, request, (void *)hid_descriptor, 9);
  if (request->bmRequestType == 0x21 && request->bRequest == 10 &&
      request->wIndex == 1 && request->wLength == 0) {
    hid_idle = request->wValue >> 8;
    return tud_control_status(rhport, request);
  }
  if (request->bmRequestType == 0xa1 && request->bRequest == 2 && request->wIndex == 1)
    return tud_control_xfer(rhport, request, &hid_idle, 1);
  return false;
}
static const usbd_class_driver_t legacy_driver = {
  .name = "TREZOR", .init = class_init, .deinit = class_deinit,
  .reset = class_reset, .open = class_open, .control_xfer_cb = cached_control,
  .xfer_cb = class_xfer,
};
const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count) {
  *count = 1;
  return &legacy_driver;
}
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                const tusb_control_request_t *request) {
  return cached_control(rhport, stage, request);
}
const uint8_t *tud_descriptor_bos_cb(void) { return cache[0].data; }
const uint16_t *__wrap_tud_descriptor_string_cb(uint8_t index, uint16_t language) {
  (void)language;
  static uint16_t descriptor[32] DRAM_ATTR;
  if (index == 0xee) return (const uint16_t *)cache[3].data;
  if (index == 0) { descriptor[0] = 0x0304; descriptor[1] = language_id; return descriptor; }
  if (index > singleton.string_count || index == 5) return NULL; /* No DebugLink */
  const char *source = singleton.strings[index - 1];
  size_t count = strnlen(source, 31);
  descriptor[0] = 0x0300 | (2 + 2 * count);
  for (size_t i = 0; i < count; i++) descriptor[i + 1] = (uint8_t)source[i];
  return descriptor;
}
