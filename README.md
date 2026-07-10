ESP32 Flight Tracker Display
---
Displays realtime flight information about the planes flying over on a cute
standalone display.

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

Thank You
---
This project depends on and thanks these libraries, services, and projects:

- [OpenSky Network](https://opensky-network.org/) - realtime ADS-B flight data
- [adsbdb](https://www.adsbdb.com/) - aircraft lookup data
- [aviationweather.gov](https://aviationweather.gov/) - airport data
- [Overpass API](https://overpass-api.de/) - map outline queries, data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright)
- [LVGL](https://github.com/lvgl/lvgl) - graphics library
- [ESP-IDF](https://github.com/espressif/esp-idf) - Espressif's SoC development framework
- [esp_lvgl_port](https://components.espressif.com/components/espressif/esp_lvgl_port) - LVGL/ESP-IDF integration
- [esp_lcd_st7796](https://components.espressif.com/components/espressif/esp_lcd_st7796) - ST7796 display driver
- [esp_lcd_touch](https://components.espressif.com/components/espressif/esp_lcd_touch) / [esp_lcd_touch_ft5x06](https://components.espressif.com/components/espressif/esp_lcd_touch_ft5x06) - touch controller drivers
- [cJSON](https://github.com/DaveGamble/cJSON) - JSON parsing

And the following projects for reference and inspiration:

- https://github.com/AnthonySturdy/micro-radar
- https://github.com/delphicchen/esp32_flight_radar
- https://github.com/MatixYo/ESP32-Plane-Radar
- https://homeding.github.io/boards/esp32s3/sc01-plus.htm

License
---
MIT, see [LICENSE](LICENSE).
