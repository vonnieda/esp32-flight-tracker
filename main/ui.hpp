#pragma once

#include "plane_table_view.hpp"
#include "radar_view.hpp"

namespace ui {

// Builds the radar screen: the radar scope on the left, a live plane list
// (tail number/altitude) filling the remaining width on the right.
void build_radar_screen(RadarView &radar, PlaneTableView &plane_table);

}  // namespace ui
