#pragma once
/* Host shape for the callbacks exercised here; firmware uses IDF's header. */
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
typedef struct esp_lcd_panel_t esp_lcd_panel_t;
struct esp_lcd_panel_t {
  esp_err_t (*reset)(esp_lcd_panel_t *);
  esp_err_t (*init)(esp_lcd_panel_t *);
  esp_err_t (*del)(esp_lcd_panel_t *);
  esp_err_t (*draw_bitmap)(esp_lcd_panel_t *, int, int, int, int, const void *);
  esp_err_t (*mirror)(esp_lcd_panel_t *, bool, bool);
  esp_err_t (*swap_xy)(esp_lcd_panel_t *, bool);
  esp_err_t (*set_gap)(esp_lcd_panel_t *, int, int);
  esp_err_t (*invert_color)(esp_lcd_panel_t *, bool);
  esp_err_t (*disp_on_off)(esp_lcd_panel_t *, bool);
};
