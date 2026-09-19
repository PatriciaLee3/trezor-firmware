#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "driver_test_hardware.h"
#include "esp_lcd_jd9853.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"

typedef struct {
  int cmd;
  size_t size;
  uint8_t data[64];
} command_t;
struct test_panel_io {
  command_t commands[128];
  unsigned count, fail_at;
  const void *pixels;
  size_t pixel_bytes;
};
static struct test_panel_io io;
static const uint8_t pixels[64] = {0x12, 0x34};

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t handle,
                                    int command, const void *data,
                                    size_t size) {
  assert(handle == &io && size <= 64 && (size == 0 || data != NULL));
  assert(io.count < 128);
  command_t *entry = &io.commands[io.count++];
  *entry = (command_t){.cmd = command, .size = size};
  if (size) memcpy(entry->data, data, size);
  return io.count == io.fail_at ? ESP_ERR_TIMEOUT : ESP_OK;
}
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t handle,
                                    int command, const void *data,
                                    size_t size) {
  assert(handle == &io && command == LCD_CMD_RAMWR);
  assert(data == pixels);
  assert(io.count < 128);
  io.commands[io.count++] = (command_t){.cmd = command, .size = size};
  if (io.count == io.fail_at) return ESP_ERR_TIMEOUT;
  io.pixels = data;
  io.pixel_bytes = size;
  return ESP_OK;
}
static void clear_io(void) {
  memset(&io, 0, sizeof(io));
  hw_delay_count = 0;
}
static esp_lcd_panel_dev_config_t configuration(unsigned bpp) {
  return (esp_lcd_panel_dev_config_t){
      .reset_gpio_num = GPIO_NUM_NC,
      .bits_per_pixel = bpp,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
  };
}
static esp_lcd_panel_handle_t create(unsigned bpp) {
  esp_lcd_panel_handle_t panel;
  esp_lcd_panel_dev_config_t config = configuration(bpp);
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_OK);
  return panel;
}

static void test_initialization(void) {
  for (unsigned bpp = 16; bpp <= 18; bpp += 2) {
    hw_reset();
    clear_io();
    esp_lcd_panel_handle_t panel = create(bpp);
    assert(panel->reset(panel) == ESP_OK);
    assert(io.count == 1 && io.commands[0].cmd == LCD_CMD_SWRESET);
    assert(hw_delay_count == 1 && hw_delays[0] == 20);
    clear_io();
    assert(panel->init(panel) == ESP_OK);
    assert(io.commands[0].cmd == LCD_CMD_SLPOUT);
    assert(io.commands[1].cmd == LCD_CMD_MADCTL);
    assert(io.commands[1].data[0] == 0);
    assert(io.commands[2].cmd == LCD_CMD_COLMOD);
    assert(io.commands[2].data[0] == (bpp == 16 ? 0x55 : 0x66));
    unsigned formats = 0, sleep_outs = 0;
    for (unsigned i = 0; i < io.count; i++) {
      if (io.commands[i].cmd == LCD_CMD_COLMOD) {
        assert((io.commands[i].data[0] & 0x0f) == (bpp == 16 ? 5 : 6));
        formats++;
      }
      if (io.commands[i].cmd == LCD_CMD_SLPOUT) sleep_outs++;
    }
    assert(formats == 2 && sleep_outs == 2);
    assert(io.commands[io.count - 1].cmd == LCD_CMD_DISPON);
    assert(hw_delay_count == 2 && hw_delays[0] == 100 && hw_delays[1] == 120);
    const unsigned init_commands = io.count;
    /* Every command failure must stop initialization at that command. */
    for (unsigned i = 1; i <= init_commands; i++) {
      clear_io();
      io.fail_at = i;
      assert(panel->init(panel) == ESP_ERR_TIMEOUT);
      assert(io.count == i);
    }
    clear_io();
    assert(panel->init(panel) == ESP_OK);
    clear_io();
    assert(panel->set_gap(panel, 0, 34) == ESP_OK);
    assert(panel->draw_bitmap(panel, 0, 0, 4, 2, pixels) == ESP_OK);
    assert(io.count == 3 && io.commands[0].cmd == LCD_CMD_CASET &&
           io.commands[1].cmd == LCD_CMD_RASET);
    const uint8_t columns[] = {0, 0, 0, 3}, rows[] = {0, 34, 0, 35};
    assert(memcmp(io.commands[0].data, columns, 4) == 0);
    assert(memcmp(io.commands[1].data, rows, 4) == 0);
    assert(io.pixels == pixels && io.pixel_bytes == 8 * (bpp == 16 ? 2u : 3u));
    assert(panel->del(panel) == ESP_OK);
    hw_assert_released();
  }
}

static void test_custom_init(void) {
  hw_reset();
  clear_io();
  const uint8_t madctl = LCD_CMD_BGR_BIT | LCD_CMD_MY_BIT;
  const uint8_t format = 0x05;
  jd9853_lcd_init_cmd_t commands[] = {
      {LCD_CMD_MADCTL, &madctl, 1, 7},
      {LCD_CMD_COLMOD, &format, 1, 9},
  };
  jd9853_vendor_config_t vendor = {.init_cmds = commands, .init_cmds_size = 2};
  esp_lcd_panel_dev_config_t config = configuration(16);
  config.vendor_config = &vendor;
  esp_lcd_panel_handle_t panel;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_OK);
  assert(panel->init(panel) == ESP_OK);
  assert(io.count == 5 && hw_delay_count == 3);
  assert(hw_delays[1] == 7 && hw_delays[2] == 9);
  clear_io();
  assert(panel->swap_xy(panel, true) == ESP_OK);
  assert(io.commands[0].data[0] == (madctl | LCD_CMD_MV_BIT));
  assert(panel->del(panel) == ESP_OK);

  config.bits_per_pixel = 18;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) ==
         ESP_ERR_NOT_SUPPORTED);
  assert(panel == NULL);
  config.bits_per_pixel = 16;
  commands[0].data = NULL;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_ERR_INVALID_ARG);
  commands[0].data = &madctl;
  commands[0].data_bytes = 0;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_ERR_INVALID_ARG);
  commands[0].data_bytes = 1;
  commands[0].cmd = 256;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_ERR_INVALID_ARG);
  vendor.init_cmds = NULL;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_ERR_INVALID_ARG);
  vendor.init_cmds_size = 0;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_OK);
  assert(panel->del(panel) == ESP_OK);
  hw_assert_released();
}

static void test_operations(void) {
  hw_reset();
  clear_io();
  esp_lcd_panel_dev_config_t config = configuration(16);
  config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
  esp_lcd_panel_handle_t panel;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_OK);
  io.fail_at = 1;
  assert(panel->mirror(panel, true, true) == ESP_ERR_TIMEOUT);
  clear_io();
  assert(panel->swap_xy(panel, true) == ESP_OK);
  assert(io.commands[0].data[0] == (LCD_CMD_BGR_BIT | LCD_CMD_MV_BIT));
  clear_io();
  io.fail_at = 1;
  assert(panel->swap_xy(panel, false) == ESP_ERR_TIMEOUT);
  clear_io();
  assert(panel->mirror(panel, false, true) == ESP_OK);
  assert(io.commands[0].data[0] ==
         (LCD_CMD_BGR_BIT | LCD_CMD_MV_BIT | LCD_CMD_MY_BIT));
  clear_io();
  assert(panel->mirror(panel, true, false) == ESP_OK);
  assert(io.commands[0].data[0] ==
         (LCD_CMD_BGR_BIT | LCD_CMD_MV_BIT | LCD_CMD_MX_BIT));
  assert(panel->swap_xy(panel, false) == ESP_OK);
  assert(io.commands[1].data[0] == (LCD_CMD_BGR_BIT | LCD_CMD_MX_BIT));
  for (unsigned i = 1; i <= 3; i++) {
    clear_io();
    io.fail_at = i;
    assert(panel->draw_bitmap(panel, 0, 0, 4, 2, pixels) == ESP_ERR_TIMEOUT);
    assert(io.count == i && io.pixels == NULL);
  }
  clear_io();
  io.fail_at = 1;
  assert(panel->reset(panel) == ESP_ERR_TIMEOUT);
  assert(hw_delay_count == 0);
  for (unsigned on = 0; on <= 1; on++) {
    clear_io();
    assert(panel->disp_on_off(panel, on) == ESP_OK);
    assert(io.commands[0].cmd == (on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF));
    clear_io();
    io.fail_at = 1;
    assert(panel->disp_on_off(panel, on) == ESP_ERR_TIMEOUT);
    clear_io();
    assert(panel->invert_color(panel, on) == ESP_OK);
    assert(io.commands[0].cmd == (on ? LCD_CMD_INVON : LCD_CMD_INVOFF));
    clear_io();
    io.fail_at = 1;
    assert(panel->invert_color(panel, on) == ESP_ERR_TIMEOUT);
  }
  clear_io();
  assert(panel->draw_bitmap(panel, 0, 0, 1, 1, NULL) == ESP_ERR_INVALID_ARG);
  assert(panel->draw_bitmap(panel, 1, 0, 1, 1, pixels) == ESP_ERR_INVALID_ARG);
  assert(panel->draw_bitmap(panel, -1, 0, 1, 1, pixels) == ESP_ERR_INVALID_ARG);
  assert(panel->draw_bitmap(panel, 0, 2, 1, 1, pixels) == ESP_ERR_INVALID_ARG);
  assert(panel->set_gap(panel, INT_MAX, INT_MIN) == ESP_OK);
  assert(panel->draw_bitmap(panel, 0, 0, 1, 1, pixels) == ESP_ERR_INVALID_ARG);
  assert(panel->set_gap(panel, -1, 0) == ESP_OK);
  assert(panel->draw_bitmap(panel, 0, 0, 1, 1, pixels) == ESP_ERR_INVALID_ARG);
  assert(panel->set_gap(panel, 65535, 0) == ESP_OK);
  assert(panel->draw_bitmap(panel, 0, 0, 2, 1, pixels) == ESP_ERR_INVALID_ARG);
  assert(io.count == 0);
  assert(panel->draw_bitmap(panel, 0, 0, 1, 1, pixels) == ESP_OK);
  assert(io.commands[0].data[0] == 0xff && io.commands[0].data[1] == 0xff);
  assert(panel->set_gap(panel, 0, 0) == ESP_OK);
  clear_io();
  assert(panel->draw_bitmap(panel, 0, 0, 65536, 65536, pixels) ==
         ESP_ERR_INVALID_ARG);
  assert(io.count == 0);
  assert(panel->del(panel) == ESP_OK);
  hw_assert_released();
}

static void test_lifecycle(void) {
  hw_reset();
  clear_io();
  esp_lcd_panel_dev_config_t config = configuration(16);
  esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)(uintptr_t)1;
  assert(esp_lcd_new_panel_jd9853(NULL, &config, &panel) ==
         ESP_ERR_INVALID_ARG);
  assert(panel == NULL);
  assert(esp_lcd_new_panel_jd9853(&io, NULL, &panel) == ESP_ERR_INVALID_ARG);
  assert(esp_lcd_new_panel_jd9853(&io, &config, NULL) == ESP_ERR_INVALID_ARG);
  config.bits_per_pixel = 24;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) ==
         ESP_ERR_NOT_SUPPORTED);
  config = configuration(16);
  config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) ==
         ESP_ERR_NOT_SUPPORTED);
  config = configuration(16);
  config.rgb_ele_order = 7;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) ==
         ESP_ERR_NOT_SUPPORTED);
  config = configuration(16);
  config.reset_gpio_num = 64;
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_ERR_INVALID_ARG);
  config = configuration(16);
  config.reset_gpio_num = 40;
  hw_fail(HW_ALLOC, 1);
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_ERR_NO_MEM);
  hw_fail(HW_CONFIG, 1);
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_ERR_TIMEOUT);
  assert(panel == NULL);
  hw_assert_released();
  assert(esp_lcd_new_panel_jd9853(&io, &config, &panel) == ESP_OK);
  for (unsigned nth = 1; nth <= 2; nth++) {
    hw_delay_count = 0;
    hw_fail(HW_LEVEL, nth);
    assert(panel->reset(panel) == ESP_ERR_TIMEOUT);
    assert(hw_delay_count == nth - 1);
  }
  hw_delay_count = 0;
  assert(panel->reset(panel) == ESP_OK);
  assert(hw_delays[0] == 10 && hw_delays[1] == 10 && hw_gpio[40].level == 1);
  hw_fail(HW_RESET, 1);
  assert(panel->del(panel) == ESP_ERR_TIMEOUT && hw_live_allocations == 1);
  assert(panel->del(panel) == ESP_OK);
  hw_assert_released();
}

int main(void) {
  test_initialization();
  test_custom_init();
  test_operations();
  test_lifecycle();
  puts("JD9853: initialization, formats, async IO, state and lifecycle passed");
}
