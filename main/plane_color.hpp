#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "lvgl.h"

// Assigns each aircraft a stable, distinguishable color derived from its
// callsign, so the same plane's blip on RadarView and row in
// PlaneTableView are visually paired without the two widgets needing to
// share any state -- they just hash the same callsign independently.
namespace plane_color {

inline lv_color_t for_callsign(const std::string &callsign) {
  static constexpr lv_color_t kPalette[] = {
      LV_COLOR_MAKE(0x39, 0xff, 0x14),  // green
      LV_COLOR_MAKE(0x00, 0xd9, 0xff),  // cyan
      LV_COLOR_MAKE(0xff, 0xd4, 0x00),  // yellow
      LV_COLOR_MAKE(0xff, 0x8a, 0x00),  // orange
      LV_COLOR_MAKE(0xff, 0x4d, 0xd2),  // magenta
      LV_COLOR_MAKE(0x8a, 0x8a, 0xff),  // periwinkle
      LV_COLOR_MAKE(0xff, 0x5a, 0x5a),  // coral
      LV_COLOR_MAKE(0x7c, 0xff, 0xb2),  // mint
  };
  constexpr size_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

  uint32_t hash = 2166136261u;  // FNV-1a
  for (char c : callsign) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  return kPalette[hash % kPaletteSize];
}

}  // namespace plane_color
