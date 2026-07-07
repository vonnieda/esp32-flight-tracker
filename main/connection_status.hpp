#pragma once

// Cross-task connection health, from "no WiFi" up through "actively
// receiving OpenSky data". Set by wifi_station.cpp (WiFi up/down) and
// main.cpp's poll loop (auth/data), read by ConnectionStatusIcon on a UI
// timer tick. Never downgraded on a failed OpenSky fetch beyond what the
// current token state implies -- see main.cpp's poll task.
namespace connection_status {

enum class State {
  kDisconnected,
  kWifiConnected,
  kAuthenticated,
  kDataFlowing,
};

void set(State state);
State get();

}  // namespace connection_status
