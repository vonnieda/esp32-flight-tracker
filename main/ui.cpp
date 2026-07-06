#include "ui.hpp"

#include "lvgl.h"

namespace {

lv_obj_t *tap_count_label = nullptr;
int tap_count = 0;

void on_test_button_clicked(lv_event_t *event) {
  (void)event;
  ++tap_count;
  lv_label_set_text_fmt(tap_count_label, "Touches: %d", tap_count);
}

}  // namespace

void ui::build_home_screen() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x10161c), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "Flight Tracker");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "Phase 1: display + touch");
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0x8fa1b3), 0);
  lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

  lv_obj_t *button = lv_button_create(screen);
  lv_obj_set_size(button, 200, 60);
  lv_obj_center(button);
  lv_obj_add_event_cb(button, on_test_button_clicked, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *button_label = lv_label_create(button);
  lv_label_set_text(button_label, "Tap to test touch");
  lv_obj_center(button_label);

  tap_count_label = lv_label_create(screen);
  lv_label_set_text(tap_count_label, "Touches: 0");
  lv_obj_set_style_text_color(tap_count_label, lv_color_white(), 0);
  lv_obj_align_to(tap_count_label, button, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
}
