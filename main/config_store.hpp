#pragma once

#include <string>

#include "esp_err.h"

// Runtime WiFi/OpenSky/home-location configuration, persisted to NVS. This
// replaces the old compile-time secrets_config.hpp: the same binary can now
// be flashed to any board and configured afterward via provisioning.hpp's
// captive portal, rather than baking credentials into the firmware image.
namespace config_store {

struct Config {
  std::string wifi_ssid;
  std::string wifi_password;
  std::string opensky_client_id;
  std::string opensky_client_secret;
  float home_latitude_deg = 0.0f;
  float home_longitude_deg = 0.0f;
};

// Initializes NVS. Must be called once at startup before load()/save().
esp_err_t init();

// Loads the saved config into out. Returns false (out left untouched) if
// no complete config is stored yet, e.g. on first boot.
bool load(Config &out);

esp_err_t save(const Config &config);

// Erases the saved config, so the next boot falls back into
// provisioning.hpp's captive portal.
esp_err_t clear();

}  // namespace config_store
