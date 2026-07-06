#include "radar_view.hpp"

#include <cmath>

#include "plane_color.hpp"

namespace {
// The scope (rings/sweep/blips) fills the whole widget so the circle
// dominates the available area; cardinal labels sit just inside the ring
// rather than carving out a separate margin band for them.
constexpr int kDiameterPx = 260;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

constexpr int kEdgeDotDiameterPx = 5;

// Matches main.cpp's OpenSky poll interval so the sweep completes one lap
// per data refresh.
constexpr uint32_t kSweepPeriodMs = 30000;

// How often blip positions are advanced by dead reckoning (speed/heading)
// in between OpenSky refreshes, so aircraft appear to move continuously
// rather than jumping every 30s.
constexpr uint32_t kMotionTickMs = 100;

// Upper bound on simultaneous contacts. Held via reserve() so blips_ never
// reallocates after a blip's lv_line has been pointed at its icon_points
// array -- reallocation would move that array and leave the line drawing
// from a dangling pointer. Sized generously since the query radius (see
// main.cpp's kQueryRangeKm) covers roughly 4x the area actually drawn on
// the scope.
constexpr size_t kMaxBlips = 64;

// Old-school phosphor-scope palette: black scope, everything else green.
constexpr lv_color_t kColorBackground = LV_COLOR_MAKE(0x00, 0x00, 0x00);
constexpr lv_color_t kColorOuterRing = LV_COLOR_MAKE(0x00, 0x8f, 0x11);
constexpr lv_color_t kColorInnerRing = LV_COLOR_MAKE(0x00, 0x4d, 0x14);
// Cardinal-direction labels are white so they stand out from the green scope.
constexpr lv_color_t kColorCompassText = LV_COLOR_MAKE(0xff, 0xff, 0xff);
constexpr lv_color_t kColorRangeText = LV_COLOR_MAKE(0x00, 0xcc, 0x33);
constexpr lv_color_t kColorCenterDot = LV_COLOR_MAKE(0x66, 0xff, 0x66);
constexpr lv_color_t kColorSweep = LV_COLOR_MAKE(0x33, 0xff, 0x33);

// A small dart/chevron pointing "up" (north) at zero heading, as offsets
// from the aircraft's own center point.
constexpr std::array<lv_point_precise_t, 5> kPlaneShapeTemplate = {{
    {0, -7}, {7, 6}, {0, 2}, {-7, 6}, {0, -7},
}};
}  // namespace

void RadarView::add_compass_label(const char *text, lv_align_t align, int x_ofs, int y_ofs) {
  lv_obj_t *label = lv_label_create(radar_area_);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, kColorCompassText, 0);
  lv_obj_align(label, align, x_ofs, y_ofs);
}

void RadarView::init(lv_obj_t *parent) {
  blips_.reserve(kMaxBlips);

  // No border on radar_area_ itself: LVGL insets a widget's content area (and
  // thus every lv_obj_center()'d/positioned child) by its own border width,
  // which would leave sweep_line_/blip icons -- sized and positioned to the
  // *outer* box below -- off-center relative to the rings and center dot.
  // The outer ring is instead drawn as its own bordered child, same as the
  // two inner rings.
  radar_area_ = lv_obj_create(parent);
  lv_obj_remove_style_all(radar_area_);
  lv_obj_set_size(radar_area_, kDiameterPx, kDiameterPx);
  lv_obj_set_style_radius(radar_area_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(radar_area_, kColorBackground, 0);
  lv_obj_set_style_bg_opa(radar_area_, LV_OPA_COVER, 0);
  lv_obj_clear_flag(radar_area_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(radar_area_);

  for (float fraction : {1.0f / 3.0f, 2.0f / 3.0f, 1.0f}) {
    lv_obj_t *ring = lv_obj_create(radar_area_);
    lv_obj_remove_style_all(ring);
    // Inset the outer ring by the border width so the stroke itself stays
    // fully inside radar_area_'s bounding box instead of getting clipped.
    const int size = static_cast<int>(kDiameterPx * fraction) - (fraction == 1.0f ? 2 : 0);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, fraction == 1.0f ? kColorOuterRing : kColorInnerRing, 0);
    lv_obj_set_style_border_width(ring, fraction == 1.0f ? 2 : 1, 0);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(ring);
  }

  radius_px_ = kDiameterPx / 2;

  // Sweep beam: an lv_line spanning the whole scope so its point coordinates
  // are plain radar_area_-local pixels. Its two points (center + rotating
  // edge point) are recomputed by hand each animation tick and the object
  // is invalidated manually -- see the class comment in radar_view.hpp for
  // why this avoids lv_obj's transform_rotation style.
  sweep_points_[0] = {radius_px_, radius_px_};
  sweep_points_[1] = {radius_px_ + radius_px_, radius_px_};

  sweep_line_ = lv_line_create(radar_area_);
  lv_obj_set_size(sweep_line_, kDiameterPx, kDiameterPx);
  lv_obj_set_pos(sweep_line_, 0, 0);
  lv_line_set_points(sweep_line_, sweep_points_.data(), 2);
  lv_obj_set_style_line_width(sweep_line_, 2, 0);
  lv_obj_set_style_line_color(sweep_line_, kColorSweep, 0);
  lv_obj_set_style_line_opa(sweep_line_, LV_OPA_COVER, 0);

  lv_anim_t sweep_anim;
  lv_anim_init(&sweep_anim);
  lv_anim_set_var(&sweep_anim, this);
  lv_anim_set_values(&sweep_anim, 0, 3600);
  lv_anim_set_duration(&sweep_anim, kSweepPeriodMs);
  lv_anim_set_repeat_count(&sweep_anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&sweep_anim, sweep_anim_cb);
  lv_anim_start(&sweep_anim);

  lv_obj_t *center_dot = lv_obj_create(radar_area_);
  lv_obj_remove_style_all(center_dot);
  lv_obj_set_size(center_dot, 6, 6);
  lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(center_dot, kColorCenterDot, 0);
  lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
  lv_obj_center(center_dot);

  add_compass_label("N", LV_ALIGN_TOP_MID, 0, 4);
  add_compass_label("E", LV_ALIGN_RIGHT_MID, -6, 0);
  add_compass_label("S", LV_ALIGN_BOTTOM_MID, 0, -4);
  add_compass_label("W", LV_ALIGN_LEFT_MID, 6, 0);

  range_label_ = lv_label_create(radar_area_);
  lv_obj_set_style_text_color(range_label_, kColorRangeText, 0);
  lv_obj_align(range_label_, LV_ALIGN_BOTTOM_RIGHT, -6, -4);

  set_range_km(range_km_);

  lv_timer_create(motion_timer_cb, kMotionTickMs, this);
}

void RadarView::set_range_km(float range_km) {
  range_km_ = range_km;
  if (range_label_ != nullptr) {
    lv_label_set_text_fmt(range_label_, "%.0f km", range_km_);
  }
}

void RadarView::set_sweep_angle_tenths(int32_t tenths) {
  const float angle_rad = (static_cast<float>(tenths) / 10.0f) * kDegToRad;
  sweep_points_[1].x = radius_px_ + static_cast<int32_t>(std::lround(radius_px_ * std::cos(angle_rad)));
  sweep_points_[1].y = radius_px_ + static_cast<int32_t>(std::lround(radius_px_ * std::sin(angle_rad)));
  lv_obj_invalidate(sweep_line_);
}

void RadarView::sweep_anim_cb(void *var, int32_t value) {
  static_cast<RadarView *>(var)->set_sweep_angle_tenths(value);
}

void RadarView::ensure_blip_count(size_t count) {
  while (blips_.size() < count) {
    Blip blip;
    blip.icon_points = kPlaneShapeTemplate;

    // Full-scope-sized so its points are plain radar_area_-local pixels,
    // matching the sweep line's approach.
    blip.icon = lv_line_create(radar_area_);
    lv_obj_set_size(blip.icon, kDiameterPx, kDiameterPx);
    lv_obj_set_pos(blip.icon, 0, 0);
    lv_line_set_points(blip.icon, blip.icon_points.data(), blip.icon_points.size());
    lv_obj_set_style_line_width(blip.icon, 2, 0);
    lv_obj_set_style_line_rounded(blip.icon, true, 0);
    lv_obj_add_flag(blip.icon, LV_OBJ_FLAG_HIDDEN);

    blip.label = lv_label_create(radar_area_);
    lv_obj_add_flag(blip.label, LV_OBJ_FLAG_HIDDEN);

    blip.edge_dot = lv_obj_create(radar_area_);
    lv_obj_remove_style_all(blip.edge_dot);
    lv_obj_set_size(blip.edge_dot, kEdgeDotDiameterPx, kEdgeDotDiameterPx);
    lv_obj_set_style_radius(blip.edge_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(blip.edge_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(blip.edge_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(blip.edge_dot, LV_OBJ_FLAG_HIDDEN);

    blips_.push_back(blip);
  }
}

void RadarView::baseline_blip(Blip &blip, const Contact &contact) {
  const float bearing_rad = contact.bearing_deg * kDegToRad;
  blip.east_km = contact.distance_km * std::sin(bearing_rad);
  blip.north_km = contact.distance_km * std::cos(bearing_rad);
  blip.speed_mps = contact.ground_speed_mps;
  blip.heading_deg = contact.track_deg;
  blip.active = true;

  const lv_color_t color = plane_color::for_callsign(contact.callsign);
  lv_obj_set_style_line_color(blip.icon, color, 0);
  lv_obj_set_style_text_color(blip.label, color, 0);
  lv_obj_set_style_bg_color(blip.edge_dot, color, 0);

  lv_label_set_text(blip.label, contact.callsign.c_str());

  // Rotate the plane-shape template to the current heading. This is the
  // only place trigonometry runs on the icon shape, and it only happens
  // once per OpenSky refresh (~30s), not on every motion tick.
  const float heading_rad = contact.track_deg * kDegToRad;
  const float cos_h = std::cos(heading_rad);
  const float sin_h = std::sin(heading_rad);
  for (size_t i = 0; i < kPlaneShapeTemplate.size(); ++i) {
    const auto &p = kPlaneShapeTemplate[i];
    blip.heading_offsets[i] = {
        static_cast<int32_t>(std::lround(p.x * cos_h - p.y * sin_h)),
        static_cast<int32_t>(std::lround(p.x * sin_h + p.y * cos_h)),
    };
  }

  // blips_ never reallocates past init()'s reserve(), but re-point the line
  // defensively in case that assumption is ever violated.
  lv_line_set_points(blip.icon, blip.icon_points.data(), blip.icon_points.size());

  reposition_blip(blip);
}

void RadarView::reposition_blip(Blip &blip) const {
  const float distance_km = std::sqrt(blip.east_km * blip.east_km + blip.north_km * blip.north_km);
  const bool in_range = distance_km <= range_km_;

  if (in_range) {
    // Center-relative offsets for the label (lv_obj_align's LV_ALIGN_CENTER
    // convention), and the same point translated to radar_area_'s top-left
    // origin for the icon's absolute line coordinates.
    const int32_t x_off = static_cast<int32_t>(std::lround((blip.east_km / range_km_) * radius_px_));
    const int32_t y_off = static_cast<int32_t>(std::lround(-(blip.north_km / range_km_) * radius_px_));
    const int32_t cx = radius_px_ + x_off;
    const int32_t cy = radius_px_ + y_off;
    for (size_t i = 0; i < blip.heading_offsets.size(); ++i) {
      blip.icon_points[i] = {cx + blip.heading_offsets[i].x, cy + blip.heading_offsets[i].y};
    }
    lv_obj_invalidate(blip.icon);
    lv_obj_align(blip.label, LV_ALIGN_CENTER, x_off, y_off + 14);
  } else if (distance_km > 0.0f) {
    // Out of display range but still within the (wider) query radius --
    // clamp a dot right on the outer ring, along the contact's true
    // bearing, instead of drawing the full heading icon.
    const int32_t x_off =
        static_cast<int32_t>(std::lround((blip.east_km / distance_km) * radius_px_));
    const int32_t y_off =
        static_cast<int32_t>(std::lround(-(blip.north_km / distance_km) * radius_px_));
    lv_obj_align(blip.edge_dot, LV_ALIGN_CENTER, x_off, y_off);
  }

  lv_obj_set_flag(blip.icon, LV_OBJ_FLAG_HIDDEN, !in_range);
  lv_obj_set_flag(blip.label, LV_OBJ_FLAG_HIDDEN, !in_range);
  lv_obj_set_flag(blip.edge_dot, LV_OBJ_FLAG_HIDDEN, in_range);
}

void RadarView::tick_motion() {
  constexpr float kDtS = static_cast<float>(kMotionTickMs) / 1000.0f;
  for (Blip &blip : blips_) {
    if (!blip.active) {
      continue;
    }
    const float heading_rad = blip.heading_deg * kDegToRad;
    const float delta_km = blip.speed_mps * kDtS / 1000.0f;
    blip.east_km += delta_km * std::sin(heading_rad);
    blip.north_km += delta_km * std::cos(heading_rad);
    reposition_blip(blip);
  }
}

void RadarView::motion_timer_cb(lv_timer_t *timer) {
  auto *self = static_cast<RadarView *>(lv_timer_get_user_data(timer));
  self->tick_motion();
}

void RadarView::update(std::span<const Contact> contacts) {
  ensure_blip_count(contacts.size());

  for (size_t i = 0; i < blips_.size(); ++i) {
    if (i < contacts.size()) {
      baseline_blip(blips_[i], contacts[i]);
    } else {
      blips_[i].active = false;
      lv_obj_add_flag(blips_[i].icon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(blips_[i].label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(blips_[i].edge_dot, LV_OBJ_FLAG_HIDDEN);
    }
  }
}
