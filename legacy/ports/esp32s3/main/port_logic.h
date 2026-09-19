#ifndef ESP32S3_PORT_LOGIC_H
#define ESP32S3_PORT_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { PORT_PACKET_SIZE = 64, PORT_PACKET_COUNT = 8 };
typedef struct {
  uint8_t packets[PORT_PACKET_COUNT][PORT_PACKET_SIZE];
  uint8_t head;
  uint8_t count;
} port_packet_ring_t;

/* The caller serializes access. Removed/reset packets are explicitly erased. */
void port_clear(void *data, size_t size);
bool port_packet_push(port_packet_ring_t *ring, const uint8_t packet[64]);
bool port_packet_pop(port_packet_ring_t *ring, uint8_t packet[64]);

typedef enum { PORT_ZONE_NONE, PORT_ZONE_CANCEL, PORT_ZONE_CONFIRM } port_zone_t;
typedef enum { PORT_TOUCH_IDLE, PORT_TOUCH_DEBOUNCE, PORT_TOUCH_ACTIVE,
               PORT_TOUCH_BLOCKED } port_touch_state_t;
typedef struct {
  port_touch_state_t state;
  port_zone_t candidate;
  uint32_t since;
} port_touch_t;
typedef struct {
  port_zone_t down;
  port_zone_t released;
} port_touch_event_t;

bool port_touch_rotate(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y);
port_zone_t port_touch_zone(uint16_t x, uint16_t y);
port_touch_event_t port_touch_update(port_touch_t *touch, uint8_t count,
                                     uint16_t x, uint16_t y, uint32_t now);
bool port_elapsed(uint32_t now, uint32_t since, uint32_t duration);

#endif
