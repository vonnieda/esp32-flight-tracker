#include "ui.hpp"

#include "lvgl.h"

namespace {
// Matches RadarView's own scope diameter so its container doesn't clip or
// leave slack around the circle.
constexpr int kRadarContainerSize = 260;
}  // namespace

void ui::build_radar_screen(RadarView &radar, PlaneTableView &plane_table) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(screen, 10, 0);
  lv_obj_set_style_pad_all(screen, 8, 0);

  lv_obj_t *radar_container = lv_obj_create(screen);
  lv_obj_remove_style_all(radar_container);
  lv_obj_set_size(radar_container, kRadarContainerSize, kRadarContainerSize);
  lv_obj_clear_flag(radar_container, LV_OBJ_FLAG_SCROLLABLE);
  radar.init(radar_container);

  plane_table.init(screen, kRadarContainerSize);
}

void ui::build_setup_screen(const char *ap_ssid) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *label = lv_label_create(screen);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, lv_pct(90));
  lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);
  lv_label_set_text_fmt(label,
                       "Setup needed\n\n"
                       "1. Connect to WiFi \"%s\"\n"
                       "2. Open http://192.168.4.1\n"
                       "3. Fill in your details",
                       ap_ssid);
}
