#pragma once

#include "lvgl.h"

// The settings screen, reached by tapping the connection status icon on the
// radar screen. Holds a two-tap "reset WiFi & config" button that clears
// config_store and reboots back into the first-boot captive portal, plus a
// button back to the radar screen.
class SettingsView {
 public:
  // Builds the screen (not shown yet). back_to is the screen the "Back"
  // button returns to.
  void init(lv_obj_t *back_to);

  lv_obj_t *screen() const { return screen_; }

 private:
  static void reset_click_cb(lv_event_t *e);
  static void back_click_cb(lv_event_t *e);
  static void revert_timer_cb(lv_timer_t *timer);

  void handle_reset_click();

  lv_obj_t *screen_ = nullptr;
  lv_obj_t *reset_label_ = nullptr;
  lv_timer_t *revert_timer_ = nullptr;
  bool armed_ = false;
};
