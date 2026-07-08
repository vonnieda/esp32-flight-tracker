#pragma once

#include <cmath>

#include "lvgl.h"

// Maps an aircraft's altitude to one of a few fixed color bands, low/warm to
// high/cool. Both RadarView and PlaneTableView derive their colors from the
// same altitude, so a plane's blip and its table row stay visually paired
// without the two widgets sharing any state.
namespace plane_color {

inline lv_color_t for_altitude_ft(float altitude_ft) {
  if (std::isnan(altitude_ft)) {
    return LV_COLOR_MAKE(0x9a, 0x9a, 0x9a);  // gray: altitude unknown
  }
  // Below 10k gets four bands instead of two -- near an airport, most
  // traffic is in this range (climbing out / descending to land), so a
  // single "yellow" band there washed out most of the picture. Bands are
  // spread evenly around the hue wheel for max contrast, not by meaning.
  if (altitude_ft < 1000.0f) {
    return LV_COLOR_MAKE(0xff, 0x00, 0x00);  // red
  }
  if (altitude_ft < 3000.0f) {
    return LV_COLOR_MAKE(0xff, 0xda, 0x00);  // gold
  }
  if (altitude_ft < 6000.0f) {
    return LV_COLOR_MAKE(0x49, 0xff, 0x00);  // green
  }
  if (altitude_ft < 10000.0f) {
    return LV_COLOR_MAKE(0x00, 0xff, 0x92);  // spring green
  }
  if (altitude_ft < 20000.0f) {
    return LV_COLOR_MAKE(0x00, 0x92, 0xff);  // azure
  }
  if (altitude_ft < 30000.0f) {
    return LV_COLOR_MAKE(0x49, 0x00, 0xff);  // violet
  }
  return LV_COLOR_MAKE(0xff, 0x00, 0xda);  // magenta
}

}  // namespace plane_color
