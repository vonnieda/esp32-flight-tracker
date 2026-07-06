#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "contact.hpp"
#include "esp_err.h"

// Fetches live aircraft state vectors from the OpenSky Network REST API
// (OAuth2 client-credentials) and converts them into Contacts relative to a
// receiver position.
class OpenSkyClient {
 public:
  // Populates out_contacts with aircraft within a bounding box around
  // (home_lat_deg, home_lon_deg) sized to cover range_km. Returns ESP_OK on
  // success (out_contacts may be empty if no traffic is nearby).
  esp_err_t fetch_contacts(float home_lat_deg, float home_lon_deg, float range_km,
                          std::vector<Contact> &out_contacts);

 private:
  esp_err_t ensure_token();
  esp_err_t fetch_token();

  std::string access_token_;
  int64_t token_expires_at_us_ = 0;
};
