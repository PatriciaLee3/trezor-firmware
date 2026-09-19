#ifndef LIBOPENCM3_USB_HID_H
#define LIBOPENCM3_USB_HID_H

#include <stdint.h>

#define USB_DT_HID 0x21
#define USB_DT_REPORT 0x22

struct usb_hid_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdHID;
  uint8_t bCountryCode;
  uint8_t bNumDescriptors;
} __attribute__((packed));

#endif
