#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "contact.hpp"
#include "lvgl.h"

// A radar-scope widget: range rings, compass labels, and a pool of aircraft
// blips (heading-oriented plane icons) plotted by bearing/distance from the
// receiver at the center.
//
// Blip icons are drawn as lv_line point arrays that we rotate/translate
// ourselves with plain trigonometry, rather than via LVGL's per-widget
// `transform_rotation` style. That style property forces every redraw of
// the widget through an offscreen-layer rotate+mask path, which is far too
// slow to keep up with a reposition tick across several aircraft (it
// previously starved the idle task and tripped the watchdog).
class RadarView {
 public:
  // Builds the scope as a child of parent, sized to fit within it.
  void init(lv_obj_t *parent);

  void set_range_km(float range_km);

  // Draws the static map outline (coastlines/borders/etc. from map_data.h)
  // as background reference lines, plotted relative to the receiver's
  // position the same way blips are. Call once, after set_range_km(), since
  // it bakes in the current range_km_ and never repositions afterward.
  void set_map_center(float home_lat_deg, float home_lon_deg);

  // Plots contacts, reusing blip widgets across calls. Contacts beyond the
  // current range are shown as a small dot clamped to the scope's edge
  // (along their true bearing) rather than a full heading icon.
  void update(std::span<const Contact> contacts);

 private:
  // A plotted aircraft: a heading-oriented icon plus a flight-number label.
  // Position is tracked in km (east/north of the receiver) rather than
  // screen pixels so it can be dead-reckoned from speed/heading between
  // OpenSky refreshes, then re-baselined to the authoritative fix on the
  // next update().
  struct Blip {
    lv_obj_t *icon = nullptr;   // Full-scope-sized lv_line; points are absolute
                                // pixel coords within radar_area_.
    lv_obj_t *label = nullptr;
    lv_obj_t *edge_dot = nullptr;  // Shown instead of icon/label when the
                                    // contact is farther than range_km_ (but
                                    // still within the wider query radius).
    std::array<lv_point_precise_t, 5> icon_points{};
    // Plane-shape points rotated to heading_deg, relative to the aircraft's
    // own center. Only recomputed when a fresh contact arrives (~30s), not
    // on every motion tick.
    std::array<lv_point_precise_t, 5> heading_offsets{};
    float east_km = 0;
    float north_km = 0;
    float speed_mps = 0;
    float heading_deg = 0;
    bool active = false;
  };

  void add_compass_label(const char *text, lv_align_t align, int x_ofs, int y_ofs);
  void ensure_blip_count(size_t count);
  void baseline_blip(Blip &blip, const Contact &contact);
  // Repositions the blip's icon/label/edge-dot for its current east/north_km.
  void reposition_blip(Blip &blip) const;
  void tick_motion();
  static void motion_timer_cb(lv_timer_t *timer);

  lv_obj_t *radar_area_ = nullptr;
  lv_obj_t *range_label_ = nullptr;
  float range_km_ = 20.0f;
  int radius_px_ = 0;
  std::vector<Blip> blips_;

  // Backing point storage for the map's lv_lines. Kept alongside map_lines_
  // for their lifetime since lv_line_set_points() just stores the pointer.
  std::vector<std::vector<lv_point_precise_t>> map_line_points_;
  std::vector<lv_obj_t *> map_lines_;
};
