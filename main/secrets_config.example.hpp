#pragma once

// Copy this file to secrets_config.hpp (gitignored) and fill in your own
// values. secrets_config.hpp, not this file, is what the build includes.
namespace secrets {

inline constexpr char kWifiSsid[] = "your-wifi-ssid";
inline constexpr char kWifiPassword[] = "your-wifi-password";

// Free account + API client at https://opensky-network.org/
inline constexpr char kOpenSkyClientId[] = "your-opensky-client-id";
inline constexpr char kOpenSkyClientSecret[] = "your-opensky-client-secret";

// Receiver's own position, used to convert aircraft lat/lon into the
// bearing/distance the radar plots against.
inline constexpr float kHomeLatitudeDeg = 0.0f;
inline constexpr float kHomeLongitudeDeg = 0.0f;

}  // namespace secrets
