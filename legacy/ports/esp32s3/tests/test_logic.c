#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "port_logic.h"
#include "waveshare_touch_lcd_1_47.h"

int main(void) {
  assert(BOARD_PANEL_SWAP_XY == 1 && BOARD_PANEL_MIRROR_X == 1 &&
         BOARD_PANEL_MIRROR_Y == 0);
  assert(BOARD_TOUCH_SWAP_XY == 1 && BOARD_TOUCH_MIRROR_X == 0 &&
         BOARD_TOUCH_MIRROR_Y == 0);
  assert(BOARD_SCREEN_WIDTH == 320 && BOARD_SCREEN_HEIGHT == 172);
  uint16_t x, y;
  /* Every raw pixel, including all four corners: centralizing the transform
   * must not change the official board's landscape mapping. */
  for (uint16_t rx = 0; rx < 172; rx++) for (uint16_t ry = 0; ry < 320; ry++) {
    assert(port_touch_rotate(rx, ry, &x, &y) && x == ry && y == rx);
  }
  assert(port_touch_rotate(171, 319, &x, &y) && x == 319 && y == 171);
  assert(!port_touch_rotate(172, 0, &x, &y));
  assert(!port_touch_rotate(0, 320, &x, &y));
  for (unsigned yy = 0; yy < 172; yy++) for (unsigned xx = 0; xx < 320; xx++) {
    port_zone_t expected = yy < 112 ? PORT_ZONE_NONE :
      xx <= 111 ? PORT_ZONE_CANCEL : xx >= 208 ? PORT_ZONE_CONFIRM : PORT_ZONE_NONE;
    assert(port_touch_zone(xx, yy) == expected);
  }
  port_touch_t t = {0};
  assert(port_touch_update(&t, 1, 319, 171, 100).down == PORT_ZONE_NONE);
  assert(port_touch_update(&t, 1, 208, 112, 114).down == PORT_ZONE_NONE);
  assert(port_touch_update(&t, 1, 208, 112, 115).down == PORT_ZONE_CONFIRM);
  assert(port_touch_update(&t, 0, 0, 0, 120).released == PORT_ZONE_CONFIRM);
  assert(port_touch_update(&t, 0, 0, 0, 121).released == PORT_ZONE_NONE);
  /* Slides, multi-touch and a gesture starting outside a zone cannot authorize. */
  for (unsigned invalid = 0; invalid < 3; invalid++) {
    memset(&t, 0, sizeof(t));
    port_touch_update(&t, 1, 250, 130, 0);
    port_touch_update(&t, 1, 250, 130, 15);
    port_touch_event_t e = port_touch_update(&t, invalid == 2 ? 2 : 1,
      invalid == 0 ? 150 : 0, 130, 16);
    assert(e.down == PORT_ZONE_NONE && e.released == PORT_ZONE_NONE);
    assert(port_touch_update(&t, 1, 250, 130, 200).down == PORT_ZONE_NONE);
    assert(port_touch_update(&t, 0, 0, 0, 201).released == PORT_ZONE_NONE);
  }
  memset(&t, 0, sizeof(t));
  port_touch_update(&t, 1, 150, 130, 0);
  assert(port_touch_update(&t, 1, 250, 130, 100).down == PORT_ZONE_NONE);
  assert(!port_elapsed(4999, 0, 5000) && port_elapsed(5000, 0, 5000));
  assert(port_elapsed(4990, UINT32_MAX - 9, 5000));
  port_packet_ring_t ring = {0};
  uint8_t packet[64], out[64];
  for (unsigned round = 0; round < 100; round++) {
    for (unsigned i = 0; i < 8; i++) {
      memset(packet, i, 64);
      assert(port_packet_push(&ring, packet));
    }
    assert(!port_packet_push(&ring, packet));
    for (unsigned i = 0; i < 8; i++) {
      assert(port_packet_pop(&ring, out));
      for (unsigned j = 0; j < 64; j++) assert(out[j] == i);
    }
    assert(!port_packet_pop(&ring, out));
    for (unsigned i = 0; i < sizeof(ring.packets); i++) assert(((uint8_t *)ring.packets)[i] == 0);
  }
  puts("touch/rotation/debounce/invalidation/5-second timing and ring tests passed");
}
