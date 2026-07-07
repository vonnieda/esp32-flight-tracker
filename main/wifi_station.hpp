#pragma once

#include <string>

#include "esp_err.h"

// Connects to an access point and transparently reconnects on drop.
class WifiStation {
 public:
  // Blocks until the first successful connection. Later drops are retried
  // in the background without blocking callers.
  esp_err_t connect(const std::string &ssid, const std::string &password);
};
