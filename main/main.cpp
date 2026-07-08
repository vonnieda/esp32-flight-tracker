#include <vector>

#include "config_store.hpp"
#include "connection_status.hpp"
#include "connection_status_icon.hpp"
#include "contact.hpp"
#include "display.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "opensky_client.hpp"
#include "plane_table_view.hpp"
#include "provisioning.hpp"
#include "radar_view.hpp"
#include "settings_view.hpp"
#include "touch.hpp"
#include "ui.hpp"
#include "wifi_station.hpp"

namespace {
constexpr char kTag[] = "main";
constexpr float kRadarRangeKm = 15.0f;
// OpenSky is queried out to twice the displayed radar range so contacts
// just beyond the scope can still show up as edge dots (and in the table)
// before they cross onto the screen.
constexpr float kQueryRangeKm = kRadarRangeKm * 2.0f;
constexpr uint32_t kPollIntervalMs = 30000;

Display display;
Touch touch;
RadarView radar;
PlaneTableView plane_table;
WifiStation wifi;
OpenSkyClient opensky;
ConnectionStatusIcon status_icon;
SettingsView settings_view;

float g_home_latitude_deg = 0.0f;
float g_home_longitude_deg = 0.0f;

void opensky_poll_task(void *arg) {
  (void)arg;
  std::vector<Contact> contacts;

  // Give the network stack a moment to settle after association (DNS/routing
  // aren't always immediately usable the instant we get an IP).
  vTaskDelay(pdMS_TO_TICKS(2000));

  while (true) {
    const esp_err_t err = opensky.fetch_contacts(g_home_latitude_deg, g_home_longitude_deg,
                                                 kQueryRangeKm, contacts);
    if (err == ESP_OK) {
      ESP_LOGI(kTag, "radar updated with %zu contacts", contacts.size());
      connection_status::set(connection_status::State::kDataFlowing);
      if (lvgl_port_lock(0)) {
        radar.update(contacts);
        plane_table.update(contacts);
        lvgl_port_unlock();
      }
    } else {
      ESP_LOGW(kTag, "OpenSky fetch failed: %s", esp_err_to_name(err));
      // Don't downgrade past what wifi_station already reflects (it sets
      // kDisconnected/kWifiConnected itself); only reflect a lost token.
      if (opensky.has_valid_token()) {
        connection_status::set(connection_status::State::kAuthenticated);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(kPollIntervalMs));
  }
}
}  // namespace

extern "C" void app_main() {
  ESP_ERROR_CHECK(config_store::init());

  lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  // Default task_affinity is -1 (unpinned), which left the LVGL task (tick +
  // layout + render + flush, all on one task) free to float onto core 0 --
  // the same core WiFi (CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0) and esp_timer
  // (CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0) are hard-pinned to. Pinning LVGL to
  // core 1 keeps it off that contention (see the FPS investigation).
  lvgl_cfg.task_affinity = 1;
  ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

  ESP_ERROR_CHECK(display.init());
  ESP_ERROR_CHECK(touch.init(display.lvgl_display()));

  config_store::Config config;
  if (!config_store::load(config)) {
    ESP_LOGI(kTag, "no saved config, entering setup mode");
    if (lvgl_port_lock(0)) {
      ui::build_setup_screen(provisioning::kApSsid);
      lvgl_port_unlock();
    }
    provisioning::run();  // Never returns; reboots once the form is submitted.
  }

  if (lvgl_port_lock(0)) {
    lv_obj_t *radar_screen = lv_screen_active();
    settings_view.init(radar_screen);
    ui::build_radar_screen(radar, plane_table, status_icon, settings_view.screen());
    radar.set_range_km(kRadarRangeKm);
    radar.set_map_center(config.home_latitude_deg, config.home_longitude_deg);
    lvgl_port_unlock();
  } else {
    ESP_LOGE(kTag, "Failed to lock LVGL for UI setup");
  }

  g_home_latitude_deg = config.home_latitude_deg;
  g_home_longitude_deg = config.home_longitude_deg;
  opensky.set_credentials(config.opensky_client_id, config.opensky_client_secret);

  ESP_ERROR_CHECK(wifi.connect(config.wifi_ssid, config.wifi_password));

  xTaskCreate(opensky_poll_task, "opensky_poll", 8192, nullptr, tskIDLE_PRIORITY + 1, nullptr);
}
