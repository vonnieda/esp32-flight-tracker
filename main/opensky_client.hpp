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
  // Optional: call once before fetch_contacts() with the client_id/secret
  // from a registered OpenSky account (https://opensky-network.org/) to use
  // OpenSky's authenticated tier. If never called (or called with an empty
  // id/secret), fetch_contacts() falls back to OpenSky's unauthenticated
  // tier, which has a much lower daily request quota -- see main.cpp's
  // is_authenticated()-gated poll interval.
  void set_credentials(std::string client_id, std::string client_secret);

  // Whether set_credentials() was given a non-empty client_id/secret, i.e.
  // fetch_contacts() will use the authenticated tier rather than anonymous
  // access.
  bool is_authenticated() const;

  // Populates out_contacts with aircraft within a bounding box around
  // (home_lat_deg, home_lon_deg) sized to cover range_km. Returns ESP_OK on
  // success (out_contacts may be empty if no traffic is nearby).
  esp_err_t fetch_contacts(float home_lat_deg, float home_lon_deg, float range_km,
                          std::vector<Contact> &out_contacts);

  // Whether a still-live OAuth2 token is currently held, i.e. the last
  // ensure_token() succeeded and it hasn't expired yet. Used by main.cpp to
  // report connection_status after a failed fetch_contacts() call. Always
  // false when running unauthenticated.
  bool has_valid_token() const;

 private:
  esp_err_t ensure_token();
  esp_err_t fetch_token();

  std::string client_id_;
  std::string client_secret_;
  std::string access_token_;
  int64_t token_expires_at_us_ = 0;
};
