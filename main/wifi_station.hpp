#pragma once

#include <string>

#include "esp_err.h"

namespace wifi_station {

// Connects to an access point, blocking until the first successful
// connection. Later drops are retried in the background without blocking.
esp_err_t connect(const std::string &ssid, const std::string &password);

}  // namespace wifi_station
