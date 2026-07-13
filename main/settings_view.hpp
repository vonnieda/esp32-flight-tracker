#pragma once

#include "lvgl.h"

// The settings screen, reached by tapping the connection status icon on the
// radar screen. Holds the non-WiFi settings (OpenSky credentials, home
// location) that used to live in provisioning.hpp's captive portal -- WiFi
// itself can only be (re)configured through the captive portal, since the
// device needs it to even boot into normal operation -- plus a two-tap
// "reset WiFi & config" button that clears config_store and reboots back
// into the captive portal, and a button back to the radar screen.
class SettingsView {
 public:
  // Builds the screen (not shown yet), pre-filled from the saved config.
  // back_to is the screen the "Back" button returns to.
  void init(lv_obj_t *back_to);

  lv_obj_t *screen() const { return screen_; }

 private:
  static void reset_click_cb(lv_event_t *e);
  static void back_click_cb(lv_event_t *e);
  static void save_click_cb(lv_event_t *e);
  static void revert_timer_cb(lv_timer_t *timer);
  static void textarea_event_cb(lv_event_t *e);
  static void keyboard_event_cb(lv_event_t *e);

  void handle_reset_click();
  void handle_save_click();

  lv_obj_t *make_field(const char *label_text, bool numeric);

  lv_obj_t *screen_ = nullptr;
  lv_obj_t *form_ = nullptr;
  lv_obj_t *keyboard_ = nullptr;
  lv_obj_t *opensky_id_ta_ = nullptr;
  lv_obj_t *opensky_secret_ta_ = nullptr;
  lv_obj_t *lat_ta_ = nullptr;
  lv_obj_t *lon_ta_ = nullptr;
  lv_obj_t *save_label_ = nullptr;
  lv_obj_t *reset_label_ = nullptr;
  lv_timer_t *revert_timer_ = nullptr;
  bool armed_ = false;
};
