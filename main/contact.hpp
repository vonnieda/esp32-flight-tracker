#pragma once

#include <span>
#include <string>

// A single aircraft, in flat-earth coordinates relative to the receiver's
// position (kilometers east/north of it).
struct Contact {
  std::string callsign;
  float east_km = 0;
  float north_km = 0;
  float altitude_ft = 0;
  float track_deg = 0;  // Ground track, degrees clockwise from north.
  float ground_speed_mps = 0;
};

// Advances each contact along its ground track by dt_s seconds, so the
// display keeps moving between OpenSky refreshes.
void dead_reckon(std::span<Contact> contacts, float dt_s);
