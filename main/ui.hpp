#pragma once

#include "connection_status_icon.hpp"
#include "plane_table_view.hpp"
#include "radar_view.hpp"

namespace ui {

// Builds the radar screen: the radar scope on the left, a live plane list
// (tail number/altitude) filling the remaining width on the right, and a
// connection status icon in the top-left corner that opens settings_screen
// when tapped.
void build_radar_screen(RadarView &radar, PlaneTableView &plane_table,
                       ConnectionStatusIcon &status_icon, lv_obj_t *settings_screen);

// Builds the first-boot setup screen shown while provisioning.hpp's captive
// portal is up, telling the user which SoftAP to join and what URL to open.
void build_setup_screen(const char *ap_ssid);

}  // namespace ui
