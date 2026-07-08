#pragma once

#include <cmath>
#include <cstdint>
#include <span>
#include <string>

// Broad airframe class, used to pick a blip icon. Derived from the ICAO type
// designator looked up per-airframe (see aircraft_type_client.hpp).
enum class PlaneClass : uint8_t {
  kUnknown,     // No type data (yet); drawn with the generic chevron.
  kAirliner,    // Large transport: Airbus/Boeing/regional jets/turboprops.
  kSmall,       // Everything fixed-wing that isn't an airliner.
  kHelicopter,
};

// A single aircraft, in flat-earth coordinates relative to the receiver's
// position (kilometers east/north of it).
struct Contact {
  std::string icao24;  // Mode S hex, lowercase; stable airframe identity.
  std::string callsign;
  float east_km = 0;
  float north_km = 0;
  float altitude_ft = NAN;  // NAN when the aircraft reported no altitude.
  float track_deg = 0;  // Ground track, degrees clockwise from north.
  float ground_speed_mps = 0;
  PlaneClass plane_class = PlaneClass::kUnknown;
};

// Advances each contact along its ground track by dt_s seconds, so the
// display keeps moving between OpenSky refreshes.
void dead_reckon(std::span<Contact> contacts, float dt_s);
