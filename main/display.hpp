#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"

// Owns the ST7796 panel bring-up (i80 bus, panel IO, panel driver, backlight)
// and registers it with esp_lvgl_port as an LVGL display.
class Display {
 public:
  // Brings up the panel and registers it with LVGL. lvgl_port_init() must
  // have been called first.
  esp_err_t init();

  void set_backlight_percent(uint8_t percent);

  lv_display_t *lvgl_display() const { return lvgl_display_; }

 private:
  esp_err_t init_backlight();
  esp_err_t init_panel();

  esp_lcd_panel_io_handle_t io_handle_ = nullptr;
  esp_lcd_panel_handle_t panel_handle_ = nullptr;
  lv_display_t *lvgl_display_ = nullptr;
};
