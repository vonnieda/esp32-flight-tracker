#pragma once

#include <vector>

#include "esp_err.h"

// Builds the radar's background map outline (coastlines, country borders,
// state/province borders) at runtime, replacing what used to be baked into
// main/map_data.h by tools/make_map.py at build time -- so the firmware
// works at any location without a per-install rebuild.
namespace map_client {

// Downloads Natural Earth 1:110m public-domain vector data (small enough to
// fetch and parse whole -- a few hundred KB total, vs. tens of MB for the
// 1:10m data tools/make_map.py used), clips it to a bounding box around
// (home_lat_deg, home_lon_deg) sized for range_km, simplifies it with
// Douglas-Peucker, and appends the result to out_outline as lat/lon pairs
// with NAN,NAN separating each polyline -- the same layout RadarView used to
// read out of MAP_OUTLINE.
//
// Best-effort across the three source files: if one fails to fetch or parse,
// the others still contribute (e.g. missing state borders beats no map at
// all). Fails only if none of them yield any lines.
esp_err_t fetch_outline(float home_lat_deg, float home_lon_deg, float range_km,
                        std::vector<float> &out_outline);

}  // namespace map_client
