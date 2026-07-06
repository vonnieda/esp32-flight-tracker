#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "contact.hpp"
#include "lvgl.h"

// A small side list showing every contact currently on the radar, with its
// flight number, live distance from the receiver, and altitude, sorted
// nearest-first. Complements RadarView, which plots position/heading but
// has no room for this much text.
class PlaneTableView {
 public:
  // Builds the list as a child of parent, growing to fill remaining flex
  // space and matching the given height.
  void init(lv_obj_t *parent, int height);

  // Repopulates the tracked aircraft and re-renders the list.
  void update(std::span<const Contact> contacts);

 private:
  // Distance is dead-reckoned from speed/heading between updates (mirroring
  // RadarView's blip motion) so it stays live rather than only changing
  // every OpenSky refresh. Tracked separately from Row: an aircraft's rank
  // by distance can change every tick as planes move, but a Row is a fixed
  // rendering slot (its on-screen position is set by widget creation order,
  // not by anything we can re-sort at runtime), so each tick re-renders
  // every row from aircraft_ in current distance order rather than trying
  // to reorder widgets.
  struct Aircraft {
    std::string callsign;
    float altitude_ft = 0;
    float east_km = 0;
    float north_km = 0;
    float speed_mps = 0;
    float heading_deg = 0;
  };

  struct Row {
    lv_obj_t *container = nullptr;
    lv_obj_t *flight_label = nullptr;
    lv_obj_t *distance_label = nullptr;
    lv_obj_t *altitude_label = nullptr;
  };

  Row &ensure_row(size_t index);
  void render();
  void tick_motion();
  static void motion_timer_cb(lv_timer_t *timer);

  lv_obj_t *list_area_ = nullptr;
  std::vector<Row> rows_;
  std::vector<Aircraft> aircraft_;
};
