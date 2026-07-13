#include "settings_view.hpp"

#include <cstdio>
#include <cstdlib>

#include "config_store.hpp"
#include "esp_log.h"
#include "esp_system.h"

namespace {
constexpr char kTag[] = "settings_view";
// How long the "tap again to confirm" state stays armed before reverting,
// so an accidental tap doesn't leave the button primed indefinitely.
constexpr uint32_t kConfirmWindowMs = 4000;
// Docked at the bottom of the screen while a text field is focused; sized by
// eye against the board's 480x320 panel (board_config.hpp) to leave enough
// room above it for the focused field to scroll into view.
constexpr int32_t kKeyboardHeightPx = 130;
}  // namespace

lv_obj_t *SettingsView::make_field(const char *label_text, bool numeric) {
  lv_obj_t *label = lv_label_create(form_);
  lv_label_set_text(label, label_text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xaaaaaa), 0);

  lv_obj_t *ta = lv_textarea_create(form_);
  lv_textarea_set_one_line(ta, true);
  lv_obj_set_width(ta, lv_pct(95));
  if (numeric) {
    // Belt-and-suspenders alongside the keyboard's NUMBER mode
    // (textarea_event_cb): filters out anything typed via a physical/BT
    // keyboard too, not just the on-screen one.
    lv_textarea_set_accepted_chars(ta, "-0123456789.");
  }
  lv_obj_add_event_cb(ta, textarea_event_cb, LV_EVENT_FOCUSED, this);
  lv_obj_add_event_cb(ta, textarea_event_cb, LV_EVENT_DEFOCUSED, this);
  return ta;
}

void SettingsView::init(lv_obj_t *back_to) {
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(screen_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(screen_, 8, 0);
  lv_obj_set_style_pad_row(screen_, 6, 0);

  lv_obj_t *title = lv_label_create(screen_);
  lv_label_set_text(title, "Settings");
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);

  // Best-effort: config_store::load() only requires WiFi creds to succeed
  // (see config_store.hpp), so on first entry after provisioning this just
  // leaves the OpenSky/location fields at their zero-value defaults.
  config_store::Config config;
  config_store::load(config);

  form_ = lv_obj_create(screen_);
  lv_obj_set_width(form_, lv_pct(100));
  lv_obj_set_flex_grow(form_, 1);
  lv_obj_set_flex_flow(form_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(form_, 4, 0);
  lv_obj_set_style_pad_all(form_, 0, 0);
  lv_obj_set_style_bg_opa(form_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(form_, 0, 0);

  opensky_id_ta_ = make_field("OpenSky Client ID", false);
  lv_textarea_set_text(opensky_id_ta_, config.opensky_client_id.c_str());

  opensky_secret_ta_ = make_field("OpenSky Client Secret", false);
  lv_textarea_set_text(opensky_secret_ta_, config.opensky_client_secret.c_str());

  char buf[32];
  lat_ta_ = make_field("Home Latitude", true);
  std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(config.home_latitude_deg));
  lv_textarea_set_text(lat_ta_, buf);

  lon_ta_ = make_field("Home Longitude", true);
  std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(config.home_longitude_deg));
  lv_textarea_set_text(lon_ta_, buf);

  lv_obj_t *button_row = lv_obj_create(screen_);
  lv_obj_set_width(button_row, lv_pct(100));
  lv_obj_set_height(button_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(button_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                       LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(button_row, 0, 0);
  lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(button_row, 0, 0);

  lv_obj_t *save_button = lv_button_create(button_row);
  lv_obj_add_event_cb(save_button, save_click_cb, LV_EVENT_CLICKED, this);
  save_label_ = lv_label_create(save_button);
  lv_label_set_text(save_label_, "Save");

  lv_obj_t *reset_button = lv_button_create(button_row);
  lv_obj_add_event_cb(reset_button, reset_click_cb, LV_EVENT_CLICKED, this);
  reset_label_ = lv_label_create(reset_button);
  lv_label_set_text(reset_label_, "Reset WiFi & Config");

  lv_obj_t *back_button = lv_button_create(button_row);
  lv_obj_add_event_cb(back_button, back_click_cb, LV_EVENT_CLICKED, back_to);
  lv_obj_t *back_label = lv_label_create(back_button);
  lv_label_set_text(back_label, "Back");

  keyboard_ = lv_keyboard_create(screen_);
  lv_obj_set_width(keyboard_, lv_pct(100));
  lv_obj_set_height(keyboard_, kKeyboardHeightPx);
  lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(keyboard_, keyboard_event_cb, LV_EVENT_READY, this);
  lv_obj_add_event_cb(keyboard_, keyboard_event_cb, LV_EVENT_CANCEL, this);
}

void SettingsView::textarea_event_cb(lv_event_t *e) {
  auto *self = static_cast<SettingsView *>(lv_event_get_user_data(e));
  auto *ta = static_cast<lv_obj_t *>(lv_event_get_target(e));
  const lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_FOCUSED) {
    const bool numeric = (ta == self->lat_ta_ || ta == self->lon_ta_);
    lv_keyboard_set_mode(self->keyboard_,
                        numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(self->keyboard_, ta);
    lv_obj_clear_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_keyboard_set_textarea(self->keyboard_, nullptr);
    lv_obj_add_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
  }
}

void SettingsView::keyboard_event_cb(lv_event_t *e) {
  auto *self = static_cast<SettingsView *>(lv_event_get_user_data(e));
  lv_keyboard_set_textarea(self->keyboard_, nullptr);
  lv_obj_add_flag(self->keyboard_, LV_OBJ_FLAG_HIDDEN);
}

void SettingsView::save_click_cb(lv_event_t *e) {
  static_cast<SettingsView *>(lv_event_get_user_data(e))->handle_save_click();
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

void SettingsView::handle_save_click() {
  // Reload rather than reuse a cached copy, so WiFi creds set via the
  // captive portal (which this screen never touches) are preserved.
  config_store::Config config;
  config_store::load(config);

  config.opensky_client_id = lv_textarea_get_text(opensky_id_ta_);
  config.opensky_client_secret = lv_textarea_get_text(opensky_secret_ta_);
  config.home_latitude_deg = std::strtof(lv_textarea_get_text(lat_ta_), nullptr);
  config.home_longitude_deg = std::strtof(lv_textarea_get_text(lon_ta_), nullptr);

  const esp_err_t err = config_store::save(config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "failed to save config: %s", esp_err_to_name(err));
    lv_label_set_text(save_label_, "Save failed");
    return;
  }

  // The new home location/OpenSky creds are picked up by main.cpp's poll
  // task only at startup (it also latches the poll interval to whichever
  // OpenSky tier applies), so restart to actually apply them -- same
  // pattern as handle_reset_click() below.
  ESP_LOGI(kTag, "settings saved, restarting");
  lv_label_set_text(save_label_, "Saved, restarting...");
  esp_restart();
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
