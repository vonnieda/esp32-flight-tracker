#include <vector>

#include "contact.hpp"
#include "display.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "opensky_client.hpp"
#include "plane_table_view.hpp"
#include "radar_view.hpp"
#include "secrets_config.hpp"
#include "touch.hpp"
#include "ui.hpp"
#include "wifi_station.hpp"

namespace {
constexpr char kTag[] = "main";
constexpr float kRadarRangeKm = 25.0f;
constexpr uint32_t kPollIntervalMs = 30000;

Display display;
Touch touch;
RadarView radar;
PlaneTableView plane_table;
WifiStation wifi;
OpenSkyClient opensky;

void opensky_poll_task(void *arg) {
  (void)arg;
  std::vector<Contact> contacts;

  // Give the network stack a moment to settle after association (DNS/routing
  // aren't always immediately usable the instant we get an IP).
  vTaskDelay(pdMS_TO_TICKS(2000));

  while (true) {
    const esp_err_t err = opensky.fetch_contacts(secrets::kHomeLatitudeDeg,
                                                 secrets::kHomeLongitudeDeg, kRadarRangeKm,
                                                 contacts);
    if (err == ESP_OK) {
      ESP_LOGI(kTag, "radar updated with %zu contacts", contacts.size());
      if (lvgl_port_lock(0)) {
        radar.update(contacts);
        plane_table.update(contacts);
        lvgl_port_unlock();
      }
    } else {
      ESP_LOGW(kTag, "OpenSky fetch failed: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
  }
}
}  // namespace

extern "C" void app_main() {
  const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

  ESP_ERROR_CHECK(display.init());
  ESP_ERROR_CHECK(touch.init(display.lvgl_display()));

  if (lvgl_port_lock(0)) {
    ui::build_radar_screen(radar, plane_table);
    radar.set_range_km(kRadarRangeKm);
    lvgl_port_unlock();
  } else {
    ESP_LOGE(kTag, "Failed to lock LVGL for UI setup");
  }

  ESP_ERROR_CHECK(wifi.connect());

  xTaskCreate(opensky_poll_task, "opensky_poll", 8192, nullptr, tskIDLE_PRIORITY + 1, nullptr);
}
