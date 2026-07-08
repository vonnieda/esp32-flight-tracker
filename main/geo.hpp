#pragma once

// Great-circle math between WGS84 coordinates.
namespace geo {

inline constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// Initial bearing from (lat1,lon1) to (lat2,lon2), degrees clockwise from
// north, in [0, 360).
float bearing_deg(float lat1_deg, float lon1_deg, float lat2_deg, float lon2_deg);

// Great-circle distance between (lat1,lon1) and (lat2,lon2), in kilometers.
float distance_km(float lat1_deg, float lon1_deg, float lat2_deg, float lon2_deg);

// Position of (lat2,lon2) relative to (lat1,lon1), as kilometers east/north.
struct EastNorth {
  float east_km;
  float north_km;
};
EastNorth east_north_km(float lat1_deg, float lon1_deg, float lat2_deg, float lon2_deg);

}  // namespace geo
