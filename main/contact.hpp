#pragma once

#include <string>

// A single aircraft plotted on the radar, in coordinates relative to the
// receiver's own position.
struct Contact {
  std::string callsign;
  float bearing_deg = 0;    // Degrees clockwise from north.
  float distance_km = 0;    // Great-circle distance from the receiver.
  float altitude_ft = 0;
  float track_deg = 0;      // Aircraft's ground track, for a future heading vector.
};
