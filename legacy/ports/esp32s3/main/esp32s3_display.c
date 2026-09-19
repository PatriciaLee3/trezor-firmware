#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_jd9853.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_axs5106.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "buttons.h"
#include "port_logic.h"
#include "port_error.h"
#include "oled.h"
#include "timer.h"
#include "waveshare_touch_lcd_1_47.h"

_Static_assert(CONFIG_ESP_LCD_TOUCH_MAX_POINTS >= 2,
               "Wallet input must preserve multi-touch samples");

#define STRIP_HEIGHT 16
#define STRIP_PIXELS (BOARD_SCREEN_WIDTH * STRIP_HEIGHT)

static const char *TAG = "trezor-display";
static esp_lcd_panel_handle_t panel;
static esp_lcd_touch_handle_t touch;
static SemaphoreHandle_t transfers;
static uint16_t *strips[2];
static bool display_ready;
static port_touch_t touch_state;
struct buttonState button;

bool esp32s3_display_ready(void) { return display_ready; }

static bool color_done(esp_lcd_panel_io_handle_t io,
                       esp_lcd_panel_io_event_data_t *event, void *ctx) {
  (void)io;
  (void)event;
  BaseType_t wake = pdFALSE;
  xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx, &wake);
  return wake == pdTRUE;
}

static void draw_strip(int index, int x, int y, int width, int height) {
  PORT_CHECK(esp_lcd_panel_draw_bitmap(panel, x, y, x + width, y + height,
                                            strips[index]));
}

static void init_backlight(void) {
  const ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_10_BIT,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = BOARD_BACKLIGHT_PWM_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  const ledc_channel_config_t channel = {
      .gpio_num = BOARD_LCD_BACKLIGHT,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LEDC_TIMER_0,
      .duty = (1023 * BOARD_BACKLIGHT_DUTY_PERCENT) / 100,
      .hpoint = 0,
  };
  PORT_CHECK(ledc_timer_config(&timer));
  PORT_CHECK(ledc_channel_config(&channel));
}

static void init_touch(void) {
  i2c_master_bus_handle_t bus;
  const i2c_master_bus_config_t bus_config = {
      .i2c_port = BOARD_TOUCH_I2C_PORT,
      .sda_io_num = BOARD_TOUCH_SDA,
      .scl_io_num = BOARD_TOUCH_SCL,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = 1,
  };
  PORT_CHECK(i2c_new_master_bus(&bus_config, &bus));
  i2c_master_dev_handle_t device;
  const i2c_device_config_t device_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = BOARD_TOUCH_ADDRESS,
      .scl_speed_hz = BOARD_TOUCH_CLOCK_HZ,
  };
  PORT_CHECK(i2c_master_bus_add_device(bus, &device_config, &device));

  const esp_lcd_touch_config_t config = {
      .x_max = BOARD_PANEL_NATIVE_WIDTH,
      .y_max = BOARD_PANEL_NATIVE_HEIGHT,
      .rst_gpio_num = BOARD_TOUCH_RST,
      .int_gpio_num = BOARD_TOUCH_INT,
      /* Leave driver transforms disabled: port_touch_rotate() applies the
       * board's complete transform exactly once, to raw coordinates. */
  };
  PORT_CHECK(esp_lcd_touch_new_i2c_axs5106(device, &config, &touch));
}

void oledInit(void) {
  transfers = xSemaphoreCreateCounting(2, 2);
  PORT_REQUIRE(transfers != NULL, "display semaphore");
  for (size_t i = 0; i < 2; i++) {
    strips[i] = heap_caps_calloc(STRIP_PIXELS, sizeof(uint16_t),
                                 MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    PORT_REQUIRE(strips[i] != NULL, "display DMA allocation");
  }

  const spi_bus_config_t bus = {
      .mosi_io_num = BOARD_LCD_MOSI,
      .miso_io_num = -1,
      .sclk_io_num = BOARD_LCD_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = STRIP_PIXELS * sizeof(uint16_t),
  };
  PORT_CHECK(spi_bus_initialize(BOARD_LCD_HOST, &bus, SPI_DMA_CH_AUTO));
  esp_lcd_panel_io_handle_t io;
  esp_lcd_panel_io_spi_config_t io_config =
      JD9853_PANEL_IO_SPI_CONFIG(BOARD_LCD_CS, BOARD_LCD_DC, color_done,
                                 transfers);
  io_config.pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ;
  PORT_CHECK(esp_lcd_new_panel_io_spi(
      (esp_lcd_spi_bus_handle_t)BOARD_LCD_HOST, &io_config, &io));
  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = BOARD_LCD_RST,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16,
  };
  PORT_CHECK(esp_lcd_new_panel_jd9853(io, &panel_config, &panel));
  PORT_CHECK(esp_lcd_panel_reset(panel));
  PORT_CHECK(esp_lcd_panel_init(panel));
  PORT_CHECK(esp_lcd_panel_invert_color(panel, BOARD_PANEL_INVERT_COLOR));
  PORT_CHECK(esp_lcd_panel_swap_xy(panel, BOARD_PANEL_SWAP_XY));
  PORT_CHECK(esp_lcd_panel_mirror(panel, BOARD_PANEL_MIRROR_X,
                                BOARD_PANEL_MIRROR_Y));
  PORT_CHECK(
      esp_lcd_panel_set_gap(panel, BOARD_PANEL_GAP_X, BOARD_PANEL_GAP_Y));
  PORT_CHECK(esp_lcd_panel_disp_on_off(panel, true));

  for (int y = 0, index = 0; y < BOARD_SCREEN_HEIGHT;
       y += STRIP_HEIGHT, index ^= 1) {
    const int height =
        BOARD_SCREEN_HEIGHT - y < STRIP_HEIGHT ? BOARD_SCREEN_HEIGHT - y
                                               : STRIP_HEIGHT;
    xSemaphoreTake(transfers, portMAX_DELAY);
    memset(strips[index], 0, STRIP_PIXELS * sizeof(uint16_t));
    draw_strip(index, 0, y, BOARD_SCREEN_WIDTH, height);
  }
  xSemaphoreTake(transfers, portMAX_DELAY);
  xSemaphoreTake(transfers, portMAX_DELAY);
  xSemaphoreGive(transfers);
  xSemaphoreGive(transfers);
  init_backlight();
  display_ready = true;
  init_touch();
  ESP_LOGI(TAG, "320x172 landscape display and touch ready");
}

void oledRefresh(void) {
  const uint8_t *source = oledGetBuffer();
  int index = 0;
  for (int out_y = 0; out_y < BOARD_OLED_HEIGHT; out_y += STRIP_HEIGHT) {
    xSemaphoreTake(transfers, portMAX_DELAY);
    uint16_t *pixels = strips[index];
    for (int dy = 0; dy < STRIP_HEIGHT; dy++) {
      const int source_y = (out_y + dy) / BOARD_OLED_SCALE;
      for (int out_x = 0; out_x < BOARD_OLED_WIDTH; out_x++) {
        const int source_x = out_x / BOARD_OLED_SCALE;
        const int offset = OLED_BUFSIZE - 1 - source_x -
                           (source_y / 8) * OLED_WIDTH;
        const int mask = 1 << (7 - source_y % 8);
        pixels[dy * BOARD_OLED_WIDTH + out_x] =
            (source[offset] & mask) ? 0xffffu : 0x0000u;
      }
    }
    draw_strip(index, BOARD_OLED_X, BOARD_OLED_Y + out_y, BOARD_OLED_WIDTH,
               STRIP_HEIGHT);
    index ^= 1;
  }
}

static port_touch_event_t touch_poll(void) {
  uint16_t x[2] = {0};
  uint16_t y[2] = {0};
  uint8_t count = 0;
  PORT_CHECK(esp_lcd_touch_read_data(touch));
  (void)esp_lcd_touch_get_coordinates(touch, x, y, NULL, &count, 2);
  uint16_t screen_x = 0xffff;
  uint16_t screen_y = 0xffff;
  if (count == 1) {
    (void)port_touch_rotate(x[0], y[0], &screen_x, &screen_y);
  }
  return port_touch_update(&touch_state, count, screen_x, screen_y, timer_ms());
}

uint16_t buttonRead(void) {
  uint16_t state = BTN_PIN_YES | BTN_PIN_NO;
  const port_touch_event_t event = touch_poll();
  if (event.down == PORT_ZONE_CONFIRM) state &= (uint16_t)~BTN_PIN_YES;
  if (event.down == PORT_ZONE_CANCEL) state &= (uint16_t)~BTN_PIN_NO;
  return state;
}

void buttonUpdate(void) {
  const port_touch_event_t event = touch_poll();
  button.YesUp = event.released == PORT_ZONE_CONFIRM;
  button.NoUp = event.released == PORT_ZONE_CANCEL;
  if (event.down == PORT_ZONE_CONFIRM) {
    if (button.YesDown < 2000000000) button.YesDown++;
  } else {
    button.YesDown = 0;
  }
  if (event.down == PORT_ZONE_CANCEL) {
    if (button.NoDown < 2000000000) button.NoDown++;
  } else {
    button.NoDown = 0;
  }
}
