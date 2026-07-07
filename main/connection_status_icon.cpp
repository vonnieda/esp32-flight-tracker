#include "connection_status_icon.hpp"

#include "connection_status.hpp"

namespace {
constexpr int kDotDiameterPx = 16;
constexpr uint32_t kTickMs = 500;

constexpr lv_color_t kColorDisconnected = LV_COLOR_MAKE(0x55, 0x55, 0x55);
constexpr lv_color_t kColorWifiConnected = LV_COLOR_MAKE(0xcc, 0xcc, 0x00);
constexpr lv_color_t kColorAuthenticated = LV_COLOR_MAKE(0xff, 0x99, 0x00);
constexpr lv_color_t kColorDataFlowing = LV_COLOR_MAKE(0x00, 0xcc, 0x33);

lv_color_t color_for_state(connection_status::State state) {
  switch (state) {
    case connection_status::State::kWifiConnected:
      return kColorWifiConnected;
    case connection_status::State::kAuthenticated:
      return kColorAuthenticated;
    case connection_status::State::kDataFlowing:
      return kColorDataFlowing;
    case connection_status::State::kDisconnected:
    default:
      return kColorDisconnected;
  }
}
}  // namespace

void ConnectionStatusIcon::init(lv_obj_t *parent, lv_obj_t *settings_screen) {
  dot_ = lv_obj_create(parent);
  lv_obj_remove_style_all(dot_);
  lv_obj_set_size(dot_, kDotDiameterPx, kDotDiameterPx);
  lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);
  lv_obj_align(dot_, LV_ALIGN_TOP_LEFT, 8, 8);
  // parent may be a flex/grid container (the radar screen is); FLOATING
  // pulls the icon out of that layout so it stays pinned to the corner.
  lv_obj_add_flag(dot_, LV_OBJ_FLAG_FLOATING);
  lv_obj_add_flag(dot_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(dot_, click_cb, LV_EVENT_CLICKED, settings_screen);

  refresh();
  lv_timer_create(tick_cb, kTickMs, this);
}

void ConnectionStatusIcon::refresh() {
  lv_obj_set_style_bg_color(dot_, color_for_state(connection_status::get()), 0);
}

void ConnectionStatusIcon::tick_cb(lv_timer_t *timer) {
  auto *self = static_cast<ConnectionStatusIcon *>(lv_timer_get_user_data(timer));
  self->refresh();
}

void ConnectionStatusIcon::click_cb(lv_event_t *e) {
  auto *settings_screen = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
  lv_screen_load(settings_screen);
}
