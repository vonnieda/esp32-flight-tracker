#include "settings_view.hpp"

#include "config_store.hpp"
#include "esp_log.h"
#include "esp_system.h"

namespace {
constexpr char kTag[] = "settings_view";
// How long the "tap again to confirm" state stays armed before reverting,
// so an accidental tap doesn't leave the button primed indefinitely.
constexpr uint32_t kConfirmWindowMs = 4000;
}  // namespace

void SettingsView::init(lv_obj_t *back_to) {
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(screen_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(screen_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(screen_, 20, 0);

  lv_obj_t *title = lv_label_create(screen_);
  lv_label_set_text(title, "Settings");
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);

  lv_obj_t *reset_button = lv_button_create(screen_);
  lv_obj_add_event_cb(reset_button, reset_click_cb, LV_EVENT_CLICKED, this);
  reset_label_ = lv_label_create(reset_button);
  lv_label_set_text(reset_label_, "Reset WiFi & Config");

  lv_obj_t *back_button = lv_button_create(screen_);
  lv_obj_add_event_cb(back_button, back_click_cb, LV_EVENT_CLICKED, back_to);
  lv_obj_t *back_label = lv_label_create(back_button);
  lv_label_set_text(back_label, "Back");
}

void SettingsView::reset_click_cb(lv_event_t *e) {
  static_cast<SettingsView *>(lv_event_get_user_data(e))->handle_reset_click();
}

void SettingsView::back_click_cb(lv_event_t *e) {
  lv_screen_load(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
}

void SettingsView::revert_timer_cb(lv_timer_t *timer) {
  auto *self = static_cast<SettingsView *>(lv_timer_get_user_data(timer));
  self->armed_ = false;
  self->revert_timer_ = nullptr;
  lv_label_set_text(self->reset_label_, "Reset WiFi & Config");
}

void SettingsView::handle_reset_click() {
  if (!armed_) {
    armed_ = true;
    lv_label_set_text(reset_label_, "Tap again to confirm");
    revert_timer_ = lv_timer_create(revert_timer_cb, kConfirmWindowMs, this);
    lv_timer_set_repeat_count(revert_timer_, 1);
    return;
  }

  if (revert_timer_ != nullptr) {
    lv_timer_delete(revert_timer_);
    revert_timer_ = nullptr;
  }
  armed_ = false;
  lv_label_set_text(reset_label_, "Resetting...");

  ESP_LOGW(kTag, "user requested config reset");
  config_store::clear();
  esp_restart();
}
