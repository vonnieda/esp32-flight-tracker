Build and Flash
---
Requires ESP-IDF (this project was built/tested against a v6.1-dev checkout)
with the environment sourced (`. $IDF_PATH/export.sh`), then:

```
idf.py set-target esp32s3
idf.py flash monitor
```

No credentials to fill in before building — on first boot (or any boot with
no saved config), the device opens an SoftAP called `FlightTracker-Setup` and
serves a captive portal. Connect a phone/laptop to it and a setup page should
pop up automatically (or open http://192.168.4.1 manually) asking for the
WiFi SSID/password, an OpenSky OAuth2 client_id/client_secret (create a free
account + API client at https://opensky-network.org/ to get one), and the
receiver's home lat/lon. Submitting the form saves it to NVS and reboots into
normal operation. To re-enter setup mode later, tap the connection status
icon in the radar screen's top-left corner to reach the settings screen, then
tap "Reset WiFi & Config" twice (the second tap, within 4s, confirms) --
this erases the saved config and reboots back into the captive portal.

Note: the board's USB-C connects directly to the ESP32-S3's native USB (no
USB-UART bridge chip), so auto-reset into the bootloader is unreliable.
If `flash` fails with "No serial data received", manually enter download
mode first: hold BOOT, tap RESET, then release BOOT.

Architecture
---
- `board_config.hpp` — all pin/bus constants for the WT32-SC01 Plus (display, touch, orientation).
- `display.hpp/.cpp`, `touch.hpp/.cpp` — panel and touch bring-up, registered with `esp_lvgl_port`.
- `contact.hpp` — the `Contact` struct (callsign/bearing_deg/distance_km/altitude_ft/track_deg/ground_speed_mps) that's the common currency between data source and rendering.
- `plane_color.hpp` — hashes a callsign to a stable color from a small fixed palette, so the same aircraft's blip on `RadarView` and row in `PlaneTableView` are visually paired without the two widgets sharing any state.
- `radar_view.hpp/.cpp` — the `RadarView` widget: rings, white compass labels sitting just inside the ring, center dot, a rotating sweep line, and pooled/reused heading-oriented plane-icon blips with flight-number labels, colored per `plane_color.hpp`. The scope fills the whole widget so the circle dominates the available area. Blip positions are dead-reckoned from speed/heading at 10Hz between OpenSky refreshes (re-baselined to the authoritative fix on each `update()`), so aircraft appear to move continuously rather than jumping every 30s. Contacts beyond the displayed range (but within the wider query radius) are drawn as a small dot clamped right onto the outer ring along their true bearing instead of the full heading icon. Takes `std::span<const Contact>` in `update()`; doesn't know or care where contacts come from.
- `plane_table_view.hpp/.cpp` — the `PlaneTableView` widget: a small side list, capped to 16 rows, of the nearest contacts' flight number (abbreviated "C/S" in the header), ground speed in knots, live distance from the receiver, and altitude (abbreviated to the nearest thousand feet with a "k" suffix above 10,000ft), sorted nearest-first and colored per `plane_color.hpp` to match each aircraft's radar blip. Includes contacts beyond `RadarView`'s displayed range. Distance is dead-reckoned from speed/heading at the same 10Hz tick as `RadarView`'s blips, and the list is re-sorted on every tick (not just each OpenSky refresh) since aircraft can change relative order as they move. Same `std::span<const Contact>` `update()` contract as `RadarView`.
- `geo.hpp/.cpp` — haversine bearing/distance between two lat/lon points.
- `wifi_station.hpp/.cpp` — STA connect (given a runtime ssid/password) with auto-reconnect; reports connection_status on connect/disconnect.
- `opensky_client.hpp/.cpp` — OAuth2 client-credentials auth + `/states/all` polling, converts raw state vectors into `Contact`s via `geo.hpp`.
- `connection_status.hpp/.cpp` — cross-task connection health enum (disconnected/wifi-connected/authenticated/data-flowing), set by `wifi_station.cpp` and `main.cpp`'s poll loop.
- `connection_status_icon.hpp/.cpp` — the tappable status dot in the radar screen's top-left corner; polls `connection_status` on a timer and navigates to the settings screen when tapped.
- `settings_view.hpp/.cpp` — the settings screen: a two-tap "Reset WiFi & Config" button (clears `config_store` and reboots into provisioning) and a "Back" button to the radar screen.
- `ui.hpp/.cpp` — screen assembly: radar scope on the left, `PlaneTableView` filling the remaining width on the right, plus the connection status icon; also the first-boot setup screen.
- `main.cpp` — wires it all together: inits display/touch, builds the settings screen, loads config (or runs provisioning if unset), builds the radar UI, connects WiFi, spawns the 30s OpenSky poll task.
- `config_store.hpp/.cpp` — NVS-backed WiFi/OpenSky/home-location config, replacing the old compile-time `secrets_config.hpp`.
- `provisioning.hpp/.cpp` — first-boot captive portal: SoftAP + spoofing DNS server + HTTP form, saves to `config_store` and reboots.
