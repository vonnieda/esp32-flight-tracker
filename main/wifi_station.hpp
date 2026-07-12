#pragma once

#include <string>

#include "esp_err.h"

namespace wifi_station {

// Connects to an access point, blocking until the first successful
// connection. Later drops are retried in the background without blocking.
esp_err_t connect(const std::string &ssid, const std::string &password);

// Forces a disconnect/reconnect cycle. For use when a caller suspects the
// link has gone "zombie" (driver still reports associated, but no data is
// getting through) -- deauths and lets the existing STA_DISCONNECTED handler
// reconnect, which a genuinely-stuck link won't recover from on its own.
void force_reconnect();

}  // namespace wifi_station
