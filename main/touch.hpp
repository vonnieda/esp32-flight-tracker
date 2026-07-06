#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

// Owns the FT6336U (FT5x06-family) touch controller and registers it with
// esp_lvgl_port as an LVGL input device.
class Touch {
 public:
  // display must be the handle returned by Display::lvgl_display().
  esp_err_t init(lv_display_t *display);

 private:
  esp_lcd_touch_handle_t touch_handle_ = nullptr;
  lv_indev_t *indev_ = nullptr;
};
