#pragma once

// First-boot setup flow, entered whenever config_store has no saved
// config. Brings up an open SoftAP ("FlightTracker-Setup") with a DNS
// server that resolves every hostname to this device, plus an HTTP server
// serving a single form (WiFi SSID/password, OpenSky client id/secret, home
// lat/lon). Most phones/laptops pop the form up automatically as a captive
// portal login page; submitting it saves the values via config_store and
// reboots into normal operation.
namespace provisioning {

// SoftAP name shown to the user; also used by main.cpp for the on-screen
// setup instructions shown while the portal is up.
inline constexpr char kApSsid[] = "FlightTracker-Setup";

// Never returns: blocks serving the portal until the form is submitted, at
// which point the device reboots.
[[noreturn]] void run();

}  // namespace provisioning
