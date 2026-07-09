#pragma once

#include <vector>

#include "esp_err.h"

// Builds the radar's background map outline (administrative boundaries) at
// runtime via the Overpass API, replacing what used to be baked into
// main/map_data.h by tools/make_map.py at build time -- so the firmware
// works at any location without a per-install rebuild.
//
// The fetch is cached to a dedicated "mapcache" flash partition keyed by the
// home coordinates it was generated for, so repeat boots at the same
// location skip the (slow, rate-limited) network round trip.
namespace map_client {

// Country/state administrative boundaries within range_km of
// (home_lat_deg, home_lon_deg). Result is appended to out_outline as lat/lon
// pairs with NAN,NAN separating each polyline.
esp_err_t fetch_admin_outline(float home_lat_deg, float home_lon_deg, float range_km,
                              std::vector<float> &out_outline);

}  // namespace map_client
