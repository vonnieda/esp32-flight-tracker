ESP32 Flight Tracker Display
---
Displays realtime flight information about the planes flying over on a cute
standalone display. Powered by ESP32 and Opensky Network.

Built with ESP-IDF + LVGL (via the IDF component manager) and modern C++.

Status
---
- [x] Phase 1: LVGL display + touch bring-up (ST7796 over i80, FT6336 touch)
- [ ] Phase 2: Radar display layout/rendering
- [ ] Phase 3: OpenSky data integration (WiFi credentials hardcoded for now)

Building
---
Requires ESP-IDF (this project was built/tested against a v6.1-dev checkout)
with the environment sourced (`. $IDF_PATH/export.sh`), then:

```
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem2301 flash monitor
```

Note: the board's USB-C connects directly to the ESP32-S3's native USB (no
USB-UART bridge chip), so auto-reset into the bootloader is unreliable.
If `flash` fails with "No serial data received", manually enter download
mode first: hold BOOT, tap RESET, then release BOOT.

Hardware notes
---
Pin mapping and bring-up parameters (i80 bus timing, BGR color order, color
byte swap, invert-color) were cross-checked against three independent,
known-working configs for this exact board: the `espp` framework's
`smartpanlee-sc01-plus` board support package
(github.com/esp-cpp/espp), the LovyanGFX device config in
sukesh-ak/ESP32-TUX, and the homeding.github.io pin reference. All three
agreed on pin numbers, giving good confidence in `main/board_config.hpp`.

The one thing that *can't* be known without the physical board is the
swap/mirror orientation, since that depends on exactly how the panel and
touch overlay are mounted. `board::kSwapXy` / `kMirrorX` / `kMirrorY` in
`main/board_config.hpp` are the single source of truth for this — flip them
there if a future revision of the board ships mirrored or rotated.

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