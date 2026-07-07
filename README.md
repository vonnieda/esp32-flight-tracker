ESP32 Flight Tracker Display
---
Displays realtime flight information about the planes flying over on a cute
standalone display. Powered by ESP32 and Opensky Network.

Built with ESP-IDF + LVGL (via the IDF component manager) and modern C++.

Architecture
---
- `board_config.hpp` — all pin/bus constants for the WT32-SC01 Plus (display, touch, orientation).
- `display.hpp/.cpp`, `touch.hpp/.cpp` — panel and touch bring-up, registered with `esp_lvgl_port`.
- `contact.hpp` — the `Contact` struct (callsign/bearing_deg/distance_km/altitude_ft/track_deg/ground_speed_mps) that's the common currency between data source and rendering.
- `plane_color.hpp` — hashes a callsign to a stable color from a small fixed palette, so the same aircraft's blip on `RadarView` and row in `PlaneTableView` are visually paired without the two widgets sharing any state.
- `radar_view.hpp/.cpp` — the `RadarView` widget: rings, white compass labels sitting just inside the ring, center dot, a rotating sweep line, and pooled/reused heading-oriented plane-icon blips with flight-number labels, colored per `plane_color.hpp`. The scope fills the whole widget so the circle dominates the available area. Blip positions are dead-reckoned from speed/heading at 10Hz between OpenSky refreshes (re-baselined to the authoritative fix on each `update()`), so aircraft appear to move continuously rather than jumping every 30s. Contacts beyond the displayed range (but within the wider query radius) are drawn as a small dot clamped right onto the outer ring along their true bearing instead of the full heading icon. Takes `std::span<const Contact>` in `update()`; doesn't know or care where contacts come from.
- `plane_table_view.hpp/.cpp` — the `PlaneTableView` widget: a small side list, capped to 16 rows, of the nearest contacts' flight number (abbreviated "C/S" in the header), ground speed in knots, live distance from the receiver, and altitude (abbreviated to the nearest thousand feet with a "k" suffix above 10,000ft), sorted nearest-first and colored per `plane_color.hpp` to match each aircraft's radar blip. Includes contacts beyond `RadarView`'s displayed range. Distance is dead-reckoned from speed/heading at the same 10Hz tick as `RadarView`'s blips, and the list is re-sorted on every tick (not just each OpenSky refresh) since aircraft can change relative order as they move. Same `std::span<const Contact>` `update()` contract as `RadarView`.
- `geo.hpp/.cpp` — haversine bearing/distance between two lat/lon points.
- `wifi_station.hpp/.cpp` — STA connect (given a runtime ssid/password) with auto-reconnect.
- `opensky_client.hpp/.cpp` — OAuth2 client-credentials auth + `/states/all` polling, converts raw state vectors into `Contact`s via `geo.hpp`.
- `ui.hpp/.cpp` — screen assembly: radar scope on the left, `PlaneTableView` filling the remaining width on the right; also the first-boot setup screen.
- `main.cpp` — wires it all together: inits display/touch, loads config (or runs provisioning if unset), builds the UI, connects WiFi, spawns the 30s OpenSky poll task.
- `config_store.hpp/.cpp` — NVS-backed WiFi/OpenSky/home-location config, replacing the old compile-time `secrets_config.hpp`.
- `provisioning.hpp/.cpp` — first-boot captive portal: SoftAP + spoofing DNS server + HTTP form, saves to `config_store` and reboots.

Building
---
Requires ESP-IDF (this project was built/tested against a v6.1-dev checkout)
with the environment sourced (`. $IDF_PATH/export.sh`), then:

```
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem2301 flash monitor
```

No credentials to fill in before building — on first boot (or any boot with
no saved config), the device opens an SoftAP called `FlightTracker-Setup` and
serves a captive portal. Connect a phone/laptop to it and a setup page should
pop up automatically (or open http://192.168.4.1 manually) asking for the
WiFi SSID/password, an OpenSky OAuth2 client_id/client_secret (create a free
account + API client at https://opensky-network.org/ to get one), and the
receiver's home lat/lon. Submitting the form saves it to NVS and reboots into
normal operation. There's currently no in-app way to re-enter setup mode
short of erasing NVS (`idf.py erase-flash`).

Note: the board's USB-C connects directly to the ESP32-S3's native USB (no
USB-UART bridge chip), so auto-reset into the bootloader is unreliable.
If `flash` fails with "No serial data received", manually enter download
mode first: hold BOOT, tap RESET, then release BOOT.

Networking notes
---
OpenSky is polled every 30s, using a registered account's OAuth2
client-credentials flow (bumps the daily credit budget from 400 to 4,000 and
the data resolution from 10s to 5s vs. anonymous access). The query radius is
2x the radar's displayed range (main.cpp's `kQueryRangeKm`, currently 20km
against a 10km scope) so contacts just outside the scope can appear as edge
dots before crossing onto it; at that radius each request still costs 1
credit, so 30s polling (2,880/day) stays comfortably under budget. The token
is refreshed automatically ~30s before its 30-minute expiry.

The app binary needs the "Single factory app (large)" partition table
(`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`, 1.5MB) — WiFi/TLS/HTTP/JSON push
it past the default 1MB partition.

This ESP-IDF checkout is a bleeding-edge `v6.1-dev` build, and its built-in
`json` (cJSON) component is currently missing a top-level `CMakeLists.txt`
(likely mid-refactor upstream). Worked around by using the managed
`espressif/cjson` component instead — worth revisiting once this moves to a
stable IDF release.

Board
---
WT32-SC01 Plus
ESP32-S3
3.5" IPS LCD + Touch

References
---
[GitHub - AnthonySturdy/micro-radar: A tiny open-source flight radar for your desk · GitHub](https://github.com/AnthonySturdy/micro-radar/tree/main)

[GitHub - delphicchen/esp32\_flight\_radar: esp32 desktop device to show flight / weather / clock integrated with Home assistant · GitHub](https://github.com/delphicchen/esp32_flight_radar)

[GitHub - MatixYo/ESP32-Plane-Radar: Open-source ESP32 firmware for a 1.28″ round display that shows live ADS-B aircraft around your location as a sonar-style plane radar. · GitHub](https://github.com/MatixYo/ESP32-Plane-Radar)

https://homeding.github.io/boards/esp32s3/sc01-plus.htm

3.5 inch, 320 * 480 px Display based on ST7796UI supporting 16 bit color 8-bit parallel interface The display is supported by the “GFX Library for Arduino”.
Touch Sensor: FT6336U on I2C, Address 0x38
ESP32-S3 processor
16 MByte Flash in QIO mode
2 MByte PSRAM (QSPI)
I2C bus using SDA=6, CLK=5
USB-C connector connected to the processor
SD Card slot

There are connectors on the board supporting RS485, Speaker and GPIO.

The Board is manufacured by Smart Panlee with id ZX3D50CE08S-USRC-4832.

Arduino CLI Configuration
For compiling with the Arduino CLI the following board settings can be used:

"board": "esp32:esp32:esp32s3"
"configuration": "JTAGAdapter=default,PSRAM=enabled,FlashMode=qio,FlashSize=16M,LoopCore=1,EventsCore=1,
  USBMode=hwcdc,CDCOnBoot=cdc,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,PartitionScheme=fatflash,
  CPUFreq=240,UploadSpeed=921600,DebugLevel=none,EraseFlash=none"

Future Improvements
---
- Flight routes https://www.adsbdb.com/
- Rain Radar https://www.rainviewer.com/
- Weather https://open-meteo.com/
