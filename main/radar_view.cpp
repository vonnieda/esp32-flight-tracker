#include "radar_view.hpp"

#include <cmath>
#include <iterator>

#include "geo.hpp"
#include "map_data.h"
#include "plane_color.hpp"

namespace {
constexpr int kRadiusPx = RadarView::kDiameterPx / 2;

constexpr int kEdgeDotDiameterPx = 5;
constexpr int kBlipLineWidthPx = 2;

// A small dart/chevron pointing "up" (north), shared by every blip's
// lv_line; each contact's track is applied with the transform_rotation
// style. The points sit centered in a kPlaneIconSizePx box, with a margin so
// the stroke isn't clipped at the box edge; the box center is the aircraft's
// position and the rotation pivot.
constexpr int kPlaneIconSizePx = 18;
constexpr lv_point_precise_t kPlaneShape[] = {
    {9, 2}, {16, 15}, {9, 11}, {2, 15}, {9, 2},
};

// Old-school phosphor-scope palette: black scope, everything else green.
constexpr lv_color_t kColorBackground = LV_COLOR_MAKE(0x00, 0x00, 0x00);
constexpr lv_color_t kColorOuterRing = LV_COLOR_MAKE(0x00, 0x8f, 0x11);
constexpr lv_color_t kColorInnerRing = LV_COLOR_MAKE(0x00, 0x4d, 0x14);
// Cardinal-direction labels are white so they stand out from the green scope.
constexpr lv_color_t kColorCompassText = LV_COLOR_MAKE(0xff, 0xff, 0xff);
constexpr lv_color_t kColorRangeText = LV_COLOR_MAKE(0x00, 0xcc, 0x33);
constexpr lv_color_t kColorCenterDot = LV_COLOR_MAKE(0x66, 0xff, 0x66);
// Dimmer than the range rings so the map reads as background context rather
// than competing with blips/rings for attention.
constexpr lv_color_t kColorMap = LV_COLOR_MAKE(0x00, 0x33, 0x0a);
}  // namespace

void RadarView::add_compass_label(const char *text, lv_align_t align, int x_ofs, int y_ofs) {
  lv_obj_t *label = lv_label_create(radar_area_);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, kColorCompassText, 0);
  lv_obj_align(label, align, x_ofs, y_ofs);
}

void RadarView::init(lv_obj_t *parent) {
  // No border on radar_area_ itself: LVGL insets a widget's content area (and
  // thus every lv_obj_center()'d/positioned child) by its own border width,
  // which would leave blip icons -- sized and positioned to the *outer* box
  // below -- off-center relative to the rings and center dot.
  // The outer ring is instead drawn as its own bordered child, same as the
  // two inner rings.
  radar_area_ = lv_obj_create(parent);
  lv_obj_remove_style_all(radar_area_);
  lv_obj_set_size(radar_area_, kDiameterPx, kDiameterPx);
  lv_obj_set_style_radius(radar_area_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(radar_area_, kColorBackground, 0);
  lv_obj_set_style_bg_opa(radar_area_, LV_OPA_COVER, 0);
  lv_obj_clear_flag(radar_area_, LV_OBJ_FLAG_SCROLLABLE);

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
}

void RadarView::set_range_km(float range_km) {
  range_km_ = range_km;
  if (range_label_ != nullptr) {
    lv_label_set_text_fmt(range_label_, "%.0f km", range_km_);
  }
}

void RadarView::set_map_center(float home_lat_deg, float home_lon_deg) {
  // MAP_OUTLINE is lat,lon pairs with NAN,NAN separating each polyline.
  std::vector<lv_point_precise_t> current;
  for (int i = 0; i + 1 < MAP_OUTLINE_LEN; i += 2) {
    const float lat = MAP_OUTLINE[i];
    const float lon = MAP_OUTLINE[i + 1];
    if (std::isnan(lat) || std::isnan(lon)) {
      if (current.size() >= 2) {
        map_line_points_.push_back(std::move(current));
      }
      current.clear();
      continue;
    }

    const geo::EastNorth position = geo::east_north_km(home_lat_deg, home_lon_deg, lat, lon);
    current.push_back({
        kRadiusPx + static_cast<int32_t>(std::lround((position.east_km / range_km_) * kRadiusPx)),
        kRadiusPx - static_cast<int32_t>(std::lround((position.north_km / range_km_) * kRadiusPx)),
    });
  }
  if (current.size() >= 2) {
    map_line_points_.push_back(std::move(current));
  }

  for (const auto &points : map_line_points_) {
    lv_obj_t *line = lv_line_create(radar_area_);
    lv_obj_set_size(line, kDiameterPx, kDiameterPx);
    lv_obj_set_pos(line, 0, 0);
    lv_line_set_points(line, points.data(), points.size());
    lv_obj_set_style_line_width(line, 1, 0);
    lv_obj_set_style_line_color(line, kColorMap, 0);
    lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
    // Behind the rings/blips, which were already created in init().
    lv_obj_move_to_index(line, 0);
  }
}

void RadarView::ensure_blip_count(size_t count) {
  while (blips_.size() < count) {
    Blip blip;

    blip.icon = lv_line_create(radar_area_);
    lv_obj_set_size(blip.icon, kPlaneIconSizePx, kPlaneIconSizePx);
    lv_line_set_points(blip.icon, kPlaneShape, std::size(kPlaneShape));
    lv_obj_set_style_line_width(blip.icon, kBlipLineWidthPx, 0);
    lv_obj_set_style_transform_pivot_x(blip.icon, kPlaneIconSizePx / 2, 0);
    lv_obj_set_style_transform_pivot_y(blip.icon, kPlaneIconSizePx / 2, 0);
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

void RadarView::update(std::span<const Contact> contacts) {
  ensure_blip_count(contacts.size());

  for (size_t i = 0; i < blips_.size(); ++i) {
    Blip &blip = blips_[i];
    if (i >= contacts.size()) {
      lv_obj_add_flag(blip.icon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(blip.label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(blip.edge_dot, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const Contact &contact = contacts[i];
    const float distance_km =
        std::sqrt(contact.east_km * contact.east_km + contact.north_km * contact.north_km);
    const bool in_range = distance_km <= range_km_;
    const lv_color_t color = plane_color::for_callsign(contact.callsign);

    if (in_range) {
      const auto x_off =
          static_cast<int32_t>(std::lround((contact.east_km / range_km_) * kRadiusPx));
      const auto y_off =
          static_cast<int32_t>(std::lround(-(contact.north_km / range_km_) * kRadiusPx));
      lv_obj_align(blip.icon, LV_ALIGN_CENTER, x_off, y_off);
      lv_obj_set_style_transform_rotation(
          blip.icon, static_cast<int32_t>(std::lround(contact.track_deg * 10.0f)), 0);
      lv_obj_set_style_line_color(blip.icon, color, 0);
      lv_label_set_text(blip.label, contact.callsign.c_str());
      lv_obj_set_style_text_color(blip.label, color, 0);
      lv_obj_align(blip.label, LV_ALIGN_CENTER, x_off, y_off + 14);
    } else if (distance_km > 0.0f) {
      // Out of display range but still within the (wider) query radius --
      // clamp a dot right on the outer ring, along the contact's true
      // bearing, instead of drawing the full plane icon.
      const auto x_off =
          static_cast<int32_t>(std::lround((contact.east_km / distance_km) * kRadiusPx));
      const auto y_off =
          static_cast<int32_t>(std::lround(-(contact.north_km / distance_km) * kRadiusPx));
      lv_obj_set_style_bg_color(blip.edge_dot, color, 0);
      lv_obj_align(blip.edge_dot, LV_ALIGN_CENTER, x_off, y_off);
    }

    lv_obj_set_flag(blip.icon, LV_OBJ_FLAG_HIDDEN, !in_range);
    lv_obj_set_flag(blip.label, LV_OBJ_FLAG_HIDDEN, !in_range);
    lv_obj_set_flag(blip.edge_dot, LV_OBJ_FLAG_HIDDEN, in_range);
  }
}
