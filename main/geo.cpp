#include "geo.hpp"

#include <cmath>

namespace {
constexpr float kEarthRadiusKm = 6371.0f;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
}  // namespace

float geo::distance_km(float lat1_deg, float lon1_deg, float lat2_deg, float lon2_deg) {
  const float lat1 = lat1_deg * kDegToRad;
  const float lat2 = lat2_deg * kDegToRad;
  const float dlat = (lat2_deg - lat1_deg) * kDegToRad;
  const float dlon = (lon2_deg - lon1_deg) * kDegToRad;

  const float a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                  std::cos(lat1) * std::cos(lat2) * std::sin(dlon / 2) * std::sin(dlon / 2);
  const float c = 2.0f * std::atan2(std::sqrt(a), std::sqrt(1.0f - a));
  return kEarthRadiusKm * c;
}

float geo::bearing_deg(float lat1_deg, float lon1_deg, float lat2_deg, float lon2_deg) {
  const float lat1 = lat1_deg * kDegToRad;
  const float lat2 = lat2_deg * kDegToRad;
  const float dlon = (lon2_deg - lon1_deg) * kDegToRad;

  const float y = std::sin(dlon) * std::cos(lat2);
  const float x =
      std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
  const float bearing = std::atan2(y, x) / kDegToRad;
  return std::fmod(bearing + 360.0f, 360.0f);
}
