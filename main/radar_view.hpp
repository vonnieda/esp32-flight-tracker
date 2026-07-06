#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "contact.hpp"
#include "lvgl.h"

// A radar-scope widget: range rings, compass labels, and a pool of aircraft
// blips plotted by bearing/distance from the receiver at the center.
class RadarView {
 public:
  // Builds the scope as a child of parent, sized to fit within it.
  void init(lv_obj_t *parent);

  void set_range_km(float range_km);

  // Plots contacts, reusing blip widgets across calls. Contacts beyond the
  // current range are hidden rather than clamped to the edge.
  void update(std::span<const Contact> contacts);

 private:
  struct Blip {
    lv_obj_t *dot = nullptr;
    lv_obj_t *label = nullptr;
  };

  void add_compass_label(const char *text, lv_align_t align, int x_ofs, int y_ofs);
  void ensure_blip_count(size_t count);
  void place_blip(Blip &blip, const Contact &contact) const;

  lv_obj_t *radar_area_ = nullptr;
  lv_obj_t *range_label_ = nullptr;
  float range_km_ = 20.0f;
  int radius_px_ = 0;
  std::vector<Blip> blips_;
};
