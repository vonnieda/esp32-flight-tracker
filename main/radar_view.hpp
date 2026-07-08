#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "contact.hpp"
#include "lvgl.h"

// A radar-scope widget: range rings, compass labels, a faint map outline,
// and one blip (heading-oriented plane icon + callsign label) per contact.
class RadarView {
 public:
  // Scope diameter; also used by ui.cpp to size the neighboring table.
  static constexpr int kDiameterPx = 260;

  // Builds the scope as a child of parent. The caller is responsible for
  // positioning it.
  void init(lv_obj_t *parent);

  void set_range_km(float range_km);

  // Draws the static map outline (coastlines/borders from map_data.h) as
  // background reference lines. Call once, after set_range_km(), since it
  // bakes in the current range and never repositions afterward.
  void set_map_center(float home_lat_deg, float home_lon_deg);

  // Plots contacts, reusing blip widgets across calls. Contacts beyond the
  // current range are shown as a small dot clamped to the scope's edge
  // (along their true bearing) rather than a full plane icon.
  void update(std::span<const Contact> contacts);

 private:
  struct Blip {
    lv_obj_t *icon = nullptr;      // Plane icon, rotated to the contact's track.
    lv_obj_t *label = nullptr;     // Callsign, drawn just below the icon.
    lv_obj_t *edge_dot = nullptr;  // Shown instead of icon/label when the
                                   // contact is beyond range_km_.
  };

  void add_compass_label(const char *text, lv_align_t align, int x_ofs, int y_ofs);
  void ensure_blip_count(size_t count);

  lv_obj_t *radar_area_ = nullptr;
  lv_obj_t *range_label_ = nullptr;
  float range_km_ = 20.0f;
  std::vector<Blip> blips_;

  // Backing point storage for the map outline's lv_lines, which keep
  // pointing into these vectors for their lifetime.
  std::vector<std::vector<lv_point_precise_t>> map_line_points_;
};
