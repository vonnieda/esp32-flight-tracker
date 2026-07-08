#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "contact.hpp"
#include "lvgl.h"

// A small table listing the nearest contacts (capped to a screenful):
// flight number, ground speed, distance from the receiver, and altitude,
// sorted nearest-first. Includes contacts beyond RadarView's display range,
// since the underlying query radius is wider than what the scope draws.
// Each row's text is colored to match its aircraft's blip on the radar (see
// plane_color.hpp).
class PlaneTableView {
 public:
  // Builds the table as a child of parent, growing to fill remaining flex
  // space and matching the given height.
  void init(lv_obj_t *parent, int height);

  // Re-renders the table from contacts, reusing row widgets across calls.
  void update(std::span<const Contact> contacts);

 private:
  struct Row {
    lv_obj_t *container = nullptr;
    lv_obj_t *flight_label = nullptr;
    lv_obj_t *speed_label = nullptr;
    lv_obj_t *distance_label = nullptr;
    lv_obj_t *altitude_label = nullptr;
  };

  void ensure_row_count(size_t count);

  lv_obj_t *list_area_ = nullptr;
  std::vector<Row> rows_;
};
