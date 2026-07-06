#include "radar_view.hpp"

#include <cmath>

namespace {
constexpr int kDiameterPx = 300;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

constexpr lv_color_t kColorBackground = LV_COLOR_MAKE(0x0a, 0x0f, 0x14);
constexpr lv_color_t kColorOuterRing = LV_COLOR_MAKE(0x25, 0x40, 0x4f);
constexpr lv_color_t kColorInnerRing = LV_COLOR_MAKE(0x1a, 0x2e, 0x38);
constexpr lv_color_t kColorCompassText = LV_COLOR_MAKE(0x5c, 0x76, 0x86);
constexpr lv_color_t kColorCenterDot = LV_COLOR_MAKE(0x55, 0xe6, 0xff);
constexpr lv_color_t kColorBlip = LV_COLOR_MAKE(0xff, 0xb7, 0x03);
constexpr lv_color_t kColorBlipLabel = LV_COLOR_MAKE(0xff, 0xe6, 0xb3);
}  // namespace

void RadarView::add_compass_label(const char *text, lv_align_t align, int x_ofs, int y_ofs) {
  lv_obj_t *label = lv_label_create(radar_area_);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, kColorCompassText, 0);
  lv_obj_align(label, align, x_ofs, y_ofs);
}

void RadarView::init(lv_obj_t *parent) {
  radar_area_ = lv_obj_create(parent);
  lv_obj_remove_style_all(radar_area_);
  lv_obj_set_size(radar_area_, kDiameterPx, kDiameterPx);
  lv_obj_set_style_radius(radar_area_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(radar_area_, kColorBackground, 0);
  lv_obj_set_style_bg_opa(radar_area_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(radar_area_, kColorOuterRing, 0);
  lv_obj_set_style_border_width(radar_area_, 2, 0);
  lv_obj_clear_flag(radar_area_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(radar_area_);

  for (float fraction : {1.0f / 3.0f, 2.0f / 3.0f}) {
    lv_obj_t *ring = lv_obj_create(radar_area_);
    lv_obj_remove_style_all(ring);
    const int size = static_cast<int>(kDiameterPx * fraction);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, kColorInnerRing, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ring);
  }

  lv_obj_t *center_dot = lv_obj_create(radar_area_);
  lv_obj_remove_style_all(center_dot);
  lv_obj_set_size(center_dot, 6, 6);
  lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(center_dot, kColorCenterDot, 0);
  lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
  lv_obj_center(center_dot);

  add_compass_label("N", LV_ALIGN_TOP_MID, 0, 4);
  add_compass_label("E", LV_ALIGN_RIGHT_MID, -6, 0);
  add_compass_label("S", LV_ALIGN_BOTTOM_MID, 0, -14);
  add_compass_label("W", LV_ALIGN_LEFT_MID, 6, 0);

  range_label_ = lv_label_create(radar_area_);
  lv_obj_set_style_text_color(range_label_, kColorCompassText, 0);
  lv_obj_align(range_label_, LV_ALIGN_BOTTOM_RIGHT, -6, -4);

  radius_px_ = kDiameterPx / 2;
  set_range_km(range_km_);
}

void RadarView::set_range_km(float range_km) {
  range_km_ = range_km;
  if (range_label_ != nullptr) {
    lv_label_set_text_fmt(range_label_, "%.0f km", range_km_);
  }
}

void RadarView::ensure_blip_count(size_t count) {
  while (blips_.size() < count) {
    Blip blip;

    blip.dot = lv_obj_create(radar_area_);
    lv_obj_remove_style_all(blip.dot);
    lv_obj_set_size(blip.dot, 8, 8);
    lv_obj_set_style_radius(blip.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(blip.dot, kColorBlip, 0);
    lv_obj_set_style_bg_opa(blip.dot, LV_OPA_COVER, 0);

    blip.label = lv_label_create(radar_area_);
    lv_obj_set_style_text_color(blip.label, kColorBlipLabel, 0);
    lv_obj_set_style_text_align(blip.label, LV_TEXT_ALIGN_CENTER, 0);

    blips_.push_back(blip);
  }
}

void RadarView::place_blip(Blip &blip, const Contact &contact) const {
  const float bearing_rad = contact.bearing_deg * kDegToRad;
  const float scaled_radius = (contact.distance_km / range_km_) * static_cast<float>(radius_px_);
  const int x = static_cast<int>(std::lround(scaled_radius * std::sin(bearing_rad)));
  const int y = static_cast<int>(std::lround(-scaled_radius * std::cos(bearing_rad)));

  lv_obj_align(blip.dot, LV_ALIGN_CENTER, x, y);
  lv_obj_align(blip.label, LV_ALIGN_CENTER, x, y + 14);
  lv_label_set_text_fmt(blip.label, "%s\nFL%03d", contact.callsign.c_str(),
                        static_cast<int>(contact.altitude_ft / 100));
}

void RadarView::update(std::span<const Contact> contacts) {
  ensure_blip_count(contacts.size());

  for (size_t i = 0; i < blips_.size(); ++i) {
    const bool visible = i < contacts.size() && contacts[i].distance_km <= range_km_;
    if (visible) {
      place_blip(blips_[i], contacts[i]);
    }
    lv_obj_set_flag(blips_[i].dot, LV_OBJ_FLAG_HIDDEN, !visible);
    lv_obj_set_flag(blips_[i].label, LV_OBJ_FLAG_HIDDEN, !visible);
  }
}
