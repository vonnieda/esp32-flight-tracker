#pragma once

#include "esp_err.h"

// Connects to the hardcoded access point in secrets_config.hpp and
// transparently reconnects on drop.
class WifiStation {
 public:
  // Blocks until the first successful connection. Later drops are retried
  // in the background without blocking callers.
  esp_err_t connect();
};
