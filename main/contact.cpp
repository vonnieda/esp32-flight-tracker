#include "contact.hpp"

#include <cmath>

#include "geo.hpp"

void dead_reckon(std::span<Contact> contacts, float dt_s) {
  for (Contact &contact : contacts) {
    const float track_rad = contact.track_deg * geo::kDegToRad;
    const float delta_km = contact.ground_speed_mps * dt_s / 1000.0f;
    contact.east_km += delta_km * std::sin(track_rad);
    contact.north_km += delta_km * std::cos(track_rad);
  }
}
