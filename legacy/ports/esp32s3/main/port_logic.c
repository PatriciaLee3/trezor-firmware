#include "port_logic.h"
#include "waveshare_touch_lcd_1_47.h"
#include <string.h>

void port_clear(void *data, size_t size) {
  volatile uint8_t *p = data;
  while (size-- != 0) *p++ = 0;
}

bool port_packet_push(port_packet_ring_t *ring, const uint8_t packet[64]) {
  if (ring->count == PORT_PACKET_COUNT) return false;
  const uint8_t slot = (ring->head + ring->count) % PORT_PACKET_COUNT;
  memcpy(ring->packets[slot], packet, PORT_PACKET_SIZE);
  ring->count++;
  return true;
}

bool port_packet_pop(port_packet_ring_t *ring, uint8_t packet[64]) {
  if (ring->count == 0) return false;
  memcpy(packet, ring->packets[ring->head], PORT_PACKET_SIZE);
  port_clear(ring->packets[ring->head], PORT_PACKET_SIZE);
  ring->head = (ring->head + 1) % PORT_PACKET_COUNT;
  ring->count--;
  return true;
}

bool port_elapsed(uint32_t now, uint32_t since, uint32_t duration) {
  return (uint32_t)(now - since) >= duration;
}

bool port_touch_rotate(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y) {
  _Static_assert(
      BOARD_SCREEN_WIDTH == (BOARD_TOUCH_SWAP_XY ? BOARD_PANEL_NATIVE_HEIGHT
                                                 : BOARD_PANEL_NATIVE_WIDTH) &&
      BOARD_SCREEN_HEIGHT == (BOARD_TOUCH_SWAP_XY ? BOARD_PANEL_NATIVE_WIDTH
                                                  : BOARD_PANEL_NATIVE_HEIGHT),
      "LCD and touch dimensions must agree");
  if (raw_x >= BOARD_PANEL_NATIVE_WIDTH || raw_y >= BOARD_PANEL_NATIVE_HEIGHT)
    return false;
  uint16_t screen_x = BOARD_TOUCH_SWAP_XY ? raw_y : raw_x;
  uint16_t screen_y = BOARD_TOUCH_SWAP_XY ? raw_x : raw_y;
  if (BOARD_TOUCH_MIRROR_X) screen_x = BOARD_SCREEN_WIDTH - 1 - screen_x;
  if (BOARD_TOUCH_MIRROR_Y) screen_y = BOARD_SCREEN_HEIGHT - 1 - screen_y;
  *x = screen_x;
  *y = screen_y;
  return true;
}

port_zone_t port_touch_zone(uint16_t x, uint16_t y) {
  if (y < BOARD_ACTION_Y_MIN || y > BOARD_ACTION_Y_MAX) return PORT_ZONE_NONE;
  if (x <= BOARD_CANCEL_X_MAX) return PORT_ZONE_CANCEL;
  if (x >= BOARD_CONFIRM_X_MIN && x <= BOARD_CONFIRM_X_MAX)
    return PORT_ZONE_CONFIRM;
  return PORT_ZONE_NONE;
}

port_touch_event_t port_touch_update(port_touch_t *touch, uint8_t count,
                                     uint16_t x, uint16_t y, uint32_t now) {
  port_touch_event_t event = {PORT_ZONE_NONE, PORT_ZONE_NONE};
  const port_zone_t zone = count == 1 ? port_touch_zone(x, y) : PORT_ZONE_NONE;
  switch (touch->state) {
    case PORT_TOUCH_IDLE:
      if (count != 0) {
        touch->candidate = zone;
        touch->since = now;
        touch->state = zone == PORT_ZONE_NONE ? PORT_TOUCH_BLOCKED
                                              : PORT_TOUCH_DEBOUNCE;
      }
      break;
    case PORT_TOUCH_DEBOUNCE:
      if (count == 0) touch->state = PORT_TOUCH_IDLE;
      else if (count != 1 || zone != touch->candidate)
        touch->state = PORT_TOUCH_BLOCKED;
      else if (port_elapsed(now, touch->since, BOARD_TOUCH_DEBOUNCE_MS))
        touch->state = PORT_TOUCH_ACTIVE;
      break;
    case PORT_TOUCH_ACTIVE:
      if (count == 0) {
        event.released = touch->candidate;
        touch->state = PORT_TOUCH_IDLE;
      } else if (count != 1 || zone != touch->candidate) {
        /* Invalidation is NOT a key-up edge (which would authorize a dialog). */
        touch->state = PORT_TOUCH_BLOCKED;
      }
      break;
    case PORT_TOUCH_BLOCKED:
      if (count == 0) touch->state = PORT_TOUCH_IDLE;
      break;
  }
  if (touch->state == PORT_TOUCH_ACTIVE) event.down = touch->candidate;
  return event;
}
