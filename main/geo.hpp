#pragma once

// Great-circle bearing and distance between two WGS84 coordinates.
namespace geo {

// Initial bearing from (lat1,lon1) to (lat2,lon2), degrees clockwise from
// north, in [0, 360).
float bearing_deg(float lat1_deg, float lon1_deg, float lat2_deg, float lon2_deg);

// Great-circle distance between (lat1,lon1) and (lat2,lon2), in kilometers.
float distance_km(float lat1_deg, float lon1_deg, float lat2_deg, float lon2_deg);

}  // namespace geo
