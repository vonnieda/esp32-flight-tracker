#include <vector>

#include "aircraft_type_client.hpp"
#include "airport_client.hpp"
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
// Between OpenSky refreshes, contacts are advanced along their last known
// track (dead reckoning) so the display keeps moving. Once a second is
// plenty for aircraft that take minutes to cross the scope.
constexpr uint32_t kDeadReckonIntervalMs = 1000;

Display display;
Touch touch;
RadarView radar;
PlaneTableView plane_table;
OpenSkyClient opensky;
AircraftTypeClient aircraft_types;
ConnectionStatusIcon status_icon;
SettingsView settings_view;

config_store::Config config;

// The shared model both views render from. Touched only under the LVGL
// lock: the dead-reckon timer runs on the LVGL task, and the poll task
// locks before replacing it.
std::vector<Contact> contacts;

void update_views() {
  radar.update(contacts);
  plane_table.update(contacts);
}

void dead_reckon_timer_cb(lv_timer_t *timer) {
  (void)timer;
  dead_reckon(contacts, kDeadReckonIntervalMs / 1000.0f);
  update_views();
}

void opensky_poll_task(void *arg) {
  (void)arg;
  std::vector<Contact> fetched;
  bool airports_loaded = false;

  // Give the network stack a moment to settle after association (DNS/routing
  // aren't always immediately usable the instant we get an IP).
  vTaskDelay(pdMS_TO_TICKS(2000));

  while (true) {
    // Airports are static; fetch them once (retrying until the first
    // success) and hand them to the radar as background scenery.
    if (!airports_loaded) {
      std::vector<Airport> airports;
      if (airport_client::fetch_airports(config.home_latitude_deg, config.home_longitude_deg,
                                         kRadarRangeKm, airports) == ESP_OK) {
        airports_loaded = true;
        if (lvgl_port_lock(0)) {
          radar.set_airports(airports);
          lvgl_port_unlock();
        }
      }
    }

    const esp_err_t err = opensky.fetch_contacts(config.home_latitude_deg,
                                                 config.home_longitude_deg, kQueryRangeKm, fetched);
    if (err == ESP_OK) {
      aircraft_types.annotate(fetched);
      ESP_LOGI(kTag, "radar updated with %zu contacts", fetched.size());
      connection_status::set(connection_status::State::kDataFlowing);
      if (lvgl_port_lock(0)) {
        contacts = fetched;
        update_views();
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
  // Pin LVGL to core 1, away from the WiFi and esp_timer tasks hard-pinned
  // to core 0; left unpinned it contends with them and rendering stutters.
  lvgl_cfg.task_affinity = 1;
  ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

  ESP_ERROR_CHECK(display.init());
  ESP_ERROR_CHECK(touch.init(display.lvgl_display()));

  if (!config_store::load(config)) {
    ESP_LOGI(kTag, "no saved config, entering setup mode");
    if (lvgl_port_lock(0)) {
      ui::build_setup_screen(provisioning::kApSsid);
      lvgl_port_unlock();
    }
    provisioning::run();
    return;  // Stays in setup mode; the device reboots once the form is submitted.
  }

  if (lvgl_port_lock(0)) {
    lv_obj_t *radar_screen = lv_screen_active();
    settings_view.init(radar_screen);
    ui::build_radar_screen(radar, plane_table, status_icon, settings_view.screen());
    radar.set_range_km(kRadarRangeKm);
    radar.set_map_center(config.home_latitude_deg, config.home_longitude_deg);
    lv_timer_create(dead_reckon_timer_cb, kDeadReckonIntervalMs, nullptr);
    lvgl_port_unlock();
  } else {
    ESP_LOGE(kTag, "Failed to lock LVGL for UI setup");
  }

  opensky.set_credentials(config.opensky_client_id, config.opensky_client_secret);
  aircraft_types.start();

  ESP_ERROR_CHECK(wifi_station::connect(config.wifi_ssid, config.wifi_password));

  xTaskCreate(opensky_poll_task, "opensky_poll", 8192, nullptr, tskIDLE_PRIORITY + 1, nullptr);
}
