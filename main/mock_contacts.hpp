#pragma once

#include <vector>

#include "contact.hpp"

// Canned contacts for exercising the radar layout before phase 3 wires up
// live OpenSky data. Remove once RadarView is fed from the real feed.
inline std::vector<Contact> mock_contacts() {
  return {
      {"UAL892", 15.0f, 8.0f, 34000.0f, 270.0f},
      {"DAL41", 95.0f, 14.0f, 28000.0f, 190.0f},
      {"ASA1234", 200.0f, 5.5f, 11000.0f, 40.0f},
      {"SWA560", 310.0f, 18.0f, 39000.0f, 95.0f},
      {"N9042B", 340.0f, 25.0f, 4500.0f, 150.0f},  // beyond default range, should be hidden
  };
}
