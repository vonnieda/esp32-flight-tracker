#include "ui.hpp"

#include "lvgl.h"

void ui::build_radar_screen(RadarView &radar) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x10161c), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  radar.init(screen);
}
