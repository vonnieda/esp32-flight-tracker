ESP32 Flight Tracker Display
---
Displays realtime flight information about the planes flying over on a cute
standalone display. Powered by ESP32 and Opensky Network.

See `DEVELOP.md` for build/flash instructions and architecture notes.

Board
---
WT32-SC01 Plus aka ZX3D50CE08S-USRC-4832
ESP32-S3, 16MB Flash, 2MB PSRAM
3.5 inch, 320 * 480 px, ST7796UI, 16 bit color, 8-bit parallel interface
Touch controller FT6336U on I2C, Address 0x38

References
---
- https://github.com/AnthonySturdy/micro-radar
- https://github.com/delphicchen/esp32_flight_radar
- https://github.com/MatixYo/ESP32-Plane-Radar
- https://homeding.github.io/boards/esp32s3/sc01-plus.htm

Future Improvements
---
- Flight routes https://www.adsbdb.com/
- Rain Radar https://www.rainviewer.com/
- Weather https://open-meteo.com/
