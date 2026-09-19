#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../main/usbd_compat.c"
#include "usb.h"
#include "u2f.h"

static bool mounted;
static bool busy[256];
static uint8_t *transfers[256];
static unsigned installs, tick, main_received, u2f_received;
static uint8_t main_order[64], u2f_order[64];
static osal_task_func_t deferred;
static void *deferred_arg;
char config_uuid_str[] = "00112233445566778899aabb";

void __fatal_error(const char *msg, const char *file, int line) {
  fprintf(stderr, "fatal %s:%d: %s\n", file, line, msg); abort();
}
void wait_random(void) {}
void delay(uint32_t count) { (void)count; }
uint32_t timer_ms(void) { return tick++; }
const uint8_t *msg_out_data(void) { return NULL; }
uint8_t *u2f_out_data(void) { return NULL; }
void msg_read_common(char type, const uint8_t *buf, int len) {
  assert(type == 'n' && len == 64); main_order[main_received++] = buf[0];
}
void msg_read_tiny(const uint8_t *buf, int len) { msg_read_common('n', buf, len); }
void u2fhid_read(char tiny, const U2FHID_FRAME *buf) {
  (void)tiny; u2f_order[u2f_received++] = ((const uint8_t *)buf)[0];
}
void vTaskDelay(uint32_t ticks) {
  tick += ticks;
  if (deferred != NULL) {
    osal_task_func_t fn = deferred;
    void *arg = deferred_arg;
    deferred = NULL;
    fn(arg);
  }
}
void usbd_defer_func(osal_task_func_t fn, void *arg, bool isr) {
  assert(!isr && deferred == NULL); deferred = fn; deferred_arg = arg;
}
bool tud_mounted(void) { return mounted; }
bool tud_disconnect(void) { mounted = false; return true; }
bool tud_connect(void) { return true; }
bool usbd_edpt_open(uint8_t port, const tusb_desc_endpoint_t *ep) {
  assert(port == 0); busy[ep->bEndpointAddress] = false; return true;
}
bool usbd_edpt_claim(uint8_t port, uint8_t ep) { assert(port == 0); return !busy[ep]; }
bool usbd_edpt_xfer(uint8_t port, uint8_t ep, uint8_t *buffer, uint16_t size, bool isr) {
  assert(port == 0 && size == 64 && !isr && !busy[ep]);
  busy[ep] = true; transfers[ep] = buffer; return true;
}
bool tud_control_xfer(uint8_t port, const tusb_control_request_t *r, void *buf, uint16_t size) {
  (void)port; (void)r; assert(buf != NULL && size > 0); return true;
}
bool tud_control_status(uint8_t port, const tusb_control_request_t *r) {
  (void)port; (void)r; return true;
}
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config) {
  installs++;
  assert(config->task.size == 8192 && config->task.xCoreID == 1);
  class_init(); return ESP_OK;
}
static void mount_device(void) {
  memset(busy, 0, sizeof(busy));
  class_reset(0);
  assert(class_open(0, (const tusb_desc_interface_t *)(config_descriptor + 9), 55) == 23);
  assert(class_open(0, (const tusb_desc_interface_t *)(config_descriptor + 32), 32) == 32);
  mounted = true;
  usbd_poll(&singleton);
}
static void host_out(uint8_t ep, uint8_t value) {
  assert(busy[ep]);
  memset(transfers[ep], value, 64);
  busy[ep] = false;
  assert(class_xfer(0, ep, XFER_RESULT_SUCCESS, 64));
}
static void equal_hex(const uint8_t *actual, size_t size, const char *hex) {
  if (strlen(hex) != size * 2) {
    fprintf(stderr, "descriptor size actual=%zu expected=%zu: %s\n", size, strlen(hex)/2, hex); abort();
  }
  for (size_t i = 0; i < size; i++) {
    unsigned expected;
    assert(sscanf(hex + i * 2, "%2x", &expected) == 1);
    if (actual[i] != expected) {
      fprintf(stderr, "descriptor %s byte %zu: actual %02x expected %02x\n", hex, i, actual[i], expected);
      abort();
    }
  }
}
int main(void) {
  usbInit();
  assert(installs == 0);
  usbd_poll(&singleton);
  assert(installs == 1);
  equal_hex(device_descriptor, 18, "12011002000000400912c153000101020301");
  equal_hex(config_descriptor, 64,
    "0902400002010080320904000002ff0000040705810340000107050103400001"
    "0904010002030000060921110100012222000705830340000107050303400001");
  equal_hex(cache[0].data, cache[0].size,
    "050f1d00011810050038b60834a909a0478bfda0768815b66500010100");
  equal_hex(cache[1].data, cache[1].size,
    "06d0f10901a1010920150026ff007508954081020921150026ff00750895409102c0");
  equal_hex(cache[2].data, cache[2].size, "1203017472657a6f722e696f2f7374617274");
  equal_hex(cache[3].data, cache[3].size, "12034d005300460054003100300030002100");
  uint8_t wcid_golden[40] = {40,0,0,0,0,1,4,0,1};
  wcid_golden[17] = 1;
  memcpy(wcid_golden + 18, "WINUSB", 6);
  assert(cache[4].size == sizeof(wcid_golden) && memcmp(cache[4].data, wcid_golden, sizeof(wcid_golden)) == 0);
  uint8_t guid_golden[146] = {0x92,0,0,0,0,1,5,0,1,0,0x88,0,0,0,7,0,0,0,42,0};
  const char *property_name = "DeviceInterfaceGUIDs";
  const char *property_value = "{0263b512-88cb-4136-9613-5c8e109d8ef5}";
  for (size_t i = 0; i < strlen(property_name); i++) guid_golden[20+2*i] = property_name[i];
  guid_golden[62] = 80;
  for (size_t i = 0; i < strlen(property_value); i++) guid_golden[66+2*i] = property_value[i];
  assert(cache[5].size == sizeof(guid_golden) && memcmp(cache[5].data, guid_golden, sizeof(guid_golden)) == 0);
  assert(__wrap_tud_descriptor_string_cb(5, 0) == NULL);
  assert(__wrap_tud_descriptor_string_cb(1, 0)[1] == 'S');
  mount_device();
  for (unsigned i = 0; i < 8; i++) { host_out(1, i); host_out(3, i + 20); }
  assert(!busy[1] && !busy[3]); /* NAK: neither OUT is armed. */
  assert(main_received == 0 && u2f_received == 0);
  for (unsigned i = 0; i < 16; i++) usbd_poll(&singleton);
  assert(main_received == 8 && u2f_received == 8);
  for (unsigned i = 0; i < 8; i++) {
    assert(main_order[i] == i && u2f_order[i] == i + 20);
  }
  uint8_t packet[64]; memset(packet, 0xa5, 64);
  assert(usbd_ep_write_packet(&singleton, 0x81, packet, 63) == 0);
  assert(usbd_ep_write_packet(&singleton, 0x81, packet, 64) == 64);
  assert(usbd_ep_write_packet(&singleton, 0x81, packet, 64) == 0);
  assert(busy[0x81] && memcmp(transfers[0x81], packet, 64) == 0);
  busy[0x81] = false; class_xfer(0, 0x81, XFER_RESULT_SUCCESS, 64);
  for (unsigned i = 0; i < 64; i++) assert(channels[0].in[i] == 0);
  host_out(1, 99);
  usbd_disconnect(&singleton, true); vTaskDelay(1);
  for (unsigned i = 0; i < sizeof(channels); i++) assert(((uint8_t *)channels)[i] == 0);
  mount_device(); host_out(1, 42); usbd_poll(&singleton);
  assert(main_received == 9 && main_order[8] == 42 && installs == 1);
  puts("Legacy USB descriptor golden, deferred install, NAK, fairness, IN and reconnect tests passed");
}
