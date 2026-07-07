#pragma once

#include "lvgl.h"

// A small tappable dot in the corner of the radar screen showing
// connection_status's current state (grey/yellow/orange/green for
// disconnected/wifi/authenticated/receiving data). Polls connection_status
// on a timer rather than being pushed to, since it's set from other tasks.
// Tapping it navigates to the settings screen.
class ConnectionStatusIcon {
 public:
  // Builds the icon as a child of parent. Tapping it loads settings_screen.
  void init(lv_obj_t *parent, lv_obj_t *settings_screen);

 private:
  static void tick_cb(lv_timer_t *timer);
  static void click_cb(lv_event_t *e);

  void refresh();

  lv_obj_t *dot_ = nullptr;
};
