#ifndef LIBOPENCM3_USB_USBD_H
#define LIBOPENCM3_USB_USBD_H

#include <stdbool.h>
#include <stdint.h>

#define USB_DT_DEVICE 1
#define USB_DT_CONFIGURATION 2
#define USB_DT_STRING 3
#define USB_DT_INTERFACE 4
#define USB_DT_ENDPOINT 5
#define USB_DT_DEVICE_SIZE 18
#define USB_DT_CONFIGURATION_SIZE 9
#define USB_DT_INTERFACE_SIZE 9
#define USB_DT_ENDPOINT_SIZE 7

#define USB_CLASS_HID 3
#define USB_CLASS_VENDOR 0xff
#define USB_ENDPOINT_ATTR_INTERRUPT 3

#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_TYPE_DIRECTION 0x80
#define USB_REQ_TYPE_IN 0x80
#define USB_REQ_TYPE_TYPE 0x60
#define USB_REQ_TYPE_STANDARD 0x00
#define USB_REQ_TYPE_VENDOR 0x40
#define USB_REQ_TYPE_RECIPIENT 0x1f
#define USB_REQ_TYPE_DEVICE 0x00
#define USB_REQ_TYPE_INTERFACE 0x01

struct usb_device_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdUSB;
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t iManufacturer;
  uint8_t iProduct;
  uint8_t iSerialNumber;
  uint8_t bNumConfigurations;
} __attribute__((packed));

struct usb_endpoint_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
  const void *extra;
  int extralen;
} __attribute__((packed));

struct usb_interface_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
  const struct usb_endpoint_descriptor *endpoint;
  const void *extra;
  int extralen;
} __attribute__((packed));

struct usb_interface {
  uint8_t num_altsetting;
  const struct usb_interface_descriptor *altsetting;
};

struct usb_config_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t wTotalLength;
  uint8_t bNumInterfaces;
  uint8_t bConfigurationValue;
  uint8_t iConfiguration;
  uint8_t bmAttributes;
  uint8_t bMaxPower;
  const struct usb_interface *interface;
} __attribute__((packed));

struct usb_string_descriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t wData[];
} __attribute__((packed));

struct usb_setup_data {
  uint8_t bmRequestType;
  uint8_t bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} __attribute__((packed));

typedef struct usbd_device usbd_device;
typedef void (*usbd_endpoint_callback)(usbd_device *, uint8_t);
typedef void (*usbd_set_config_callback)(usbd_device *, uint16_t);
typedef void (*usbd_control_complete_callback)(usbd_device *,
                                               struct usb_setup_data *);

enum usbd_request_return_codes {
  USBD_REQ_NOTSUPP = 0,
  USBD_REQ_HANDLED = 1,
  USBD_REQ_NEXT_CALLBACK = 2,
};

typedef enum usbd_request_return_codes (*usbd_control_callback)(
    usbd_device *, struct usb_setup_data *, uint8_t **, uint16_t *,
    usbd_control_complete_callback *);

struct _usbd_driver {
  uint8_t unused;
};
extern const struct _usbd_driver otgfs_usb_driver;

usbd_device *usbd_init(const struct _usbd_driver *driver,
                       const struct usb_device_descriptor *device,
                       const struct usb_config_descriptor *config,
                       const char *const *strings, int string_count,
                       uint8_t *control_buffer, uint16_t control_buffer_size);
void usbd_poll(usbd_device *device);
void usbd_ep_setup(usbd_device *device, uint8_t address, uint8_t type,
                   uint16_t max_size, usbd_endpoint_callback callback);
uint16_t usbd_ep_read_packet(usbd_device *device, uint8_t address, void *buffer,
                             uint16_t length);
uint16_t usbd_ep_write_packet(usbd_device *device, uint8_t address,
                              const void *buffer, uint16_t length);
void usbd_register_control_callback(usbd_device *device, uint8_t type,
                                    uint8_t type_mask,
                                    usbd_control_callback callback);
void usbd_register_set_config_callback(usbd_device *device,
                                       usbd_set_config_callback callback);
void usbd_disconnect(usbd_device *device, bool disconnected);

#endif
