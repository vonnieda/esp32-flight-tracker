#include <atomic>
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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "map_client.hpp"
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
constexpr uint32_t kAuthenticatedPollIntervalMs = 30000;
// OpenSky's anonymous (unauthenticated) tier has a much smaller daily request
// quota than a registered client-credentials account, so once no OpenSky
// credentials are configured (settings_view.hpp), poll far less often to
// stay under it.
constexpr uint32_t kUnauthenticatedPollIntervalMs = 5 * 60 * 1000;
// Between OpenSky refreshes, contacts are advanced along their last known
// track (dead reckoning) so the display keeps moving. Once a second is
// plenty for aircraft that take minutes to cross the scope.
constexpr uint32_t kDeadReckonIntervalMs = 1000;

// If OpenSky data hasn't refreshed in a while, contacts are cleared rather
// than kept dead-reckoning indefinitely -- otherwise an extended outage (e.g.
// a WiFi link gone stale overnight) walks planes thousands of km off their
// real position before the next successful fetch snaps them back. Expressed
// as a multiple of missed polls, generous enough to ride out a transient
// hiccup, rather than a fixed duration, since the poll interval itself varies
// between the authenticated and unauthenticated tiers.
constexpr int kStaleAfterMissedPolls = 6;

// Set once at startup from whether OpenSky credentials are configured (see
// app_main); read by both the dead-reckon timer and the poll task, which is
// safe since it's fixed before either starts running.
uint32_t g_poll_interval_ms = kAuthenticatedPollIntervalMs;

// If that many consecutive polls fail while WiFi still reports itself
// connected, the link is presumed zombied (associated but not passing data,
// as modem-sleep power save can cause after an idle stretch) and gets a
// forced disconnect/reconnect to recover it.
constexpr int kMaxConsecutiveFetchFailures = 6;

// Old-school phosphor-scope palette: dimmer than the range rings so the map
// reads as background context rather than competing with blips/rings.
constexpr lv_color_t kColorMapAdmin = LV_COLOR_MAKE(0x00, 0x33, 0x0a);

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

// Timestamp of the last successful OpenSky fetch, read by the LVGL-task dead
// reckon timer and written by the poll task -- atomic since it's a lone
// scalar not otherwise covered by the LVGL-lock convention above.
std::atomic<int64_t> last_fetch_success_us{0};

void update_views() {
  radar.update(contacts);
  plane_table.update(contacts);
}

void dead_reckon_timer_cb(lv_timer_t *timer) {
  (void)timer;
  const int64_t stale_us =
      kStaleAfterMissedPolls * static_cast<int64_t>(g_poll_interval_ms) * 1000LL;
  const int64_t last_success = last_fetch_success_us.load(std::memory_order_relaxed);
  if (last_success != 0 && esp_timer_get_time() - last_success > stale_us) {
    if (!contacts.empty()) {
      ESP_LOGW(kTag, "no data for over %lld s, clearing stale contacts",
               static_cast<long long>(stale_us / 1000000));
      contacts.clear();
      update_views();
    }
    return;
  }
  dead_reckon(contacts, kDeadReckonIntervalMs / 1000.0f);
  update_views();
}

void opensky_poll_task(void *arg) {
  (void)arg;
  std::vector<Contact> fetched;
  bool airports_loaded = false;
  bool admin_outline_loaded = false;
  int consecutive_fetch_failures = 0;

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

    // Map outline is static too; fetch and simplify it once (retrying until
    // the first success) rather than baking a location in at build time.
    if (!admin_outline_loaded) {
      std::vector<float> outline;
      if (map_client::fetch_admin_outline(config.home_latitude_deg, config.home_longitude_deg,
                                          kRadarRangeKm, outline) == ESP_OK) {
        admin_outline_loaded = true;
        if (lvgl_port_lock(0)) {
          radar.add_map_outline(config.home_latitude_deg, config.home_longitude_deg, outline,
                                kColorMapAdmin);
          lvgl_port_unlock();
        }
      }
    }

    const esp_err_t err = opensky.fetch_contacts(config.home_latitude_deg,
                                                 config.home_longitude_deg, kQueryRangeKm, fetched);
    if (err == ESP_OK) {
      consecutive_fetch_failures = 0;
      last_fetch_success_us.store(esp_timer_get_time(), std::memory_order_relaxed);
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

      // wifi_station only notices a drop via STA_DISCONNECTED, which a
      // zombied-but-still-associated link never fires. If fetches keep
      // failing while WiFi still claims to be connected, force the issue.
      ++consecutive_fetch_failures;
      if (consecutive_fetch_failures >= kMaxConsecutiveFetchFailures &&
          connection_status::get() != connection_status::State::kDisconnected) {
        ESP_LOGW(kTag, "%d consecutive fetch failures, forcing WiFi reconnect",
                consecutive_fetch_failures);
        wifi_station::force_reconnect();
        consecutive_fetch_failures = 0;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(g_poll_interval_ms));
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

  opensky.set_credentials(config.opensky_client_id, config.opensky_client_secret);
  g_poll_interval_ms =
      opensky.is_authenticated() ? kAuthenticatedPollIntervalMs : kUnauthenticatedPollIntervalMs;
  if (!opensky.is_authenticated()) {
    ESP_LOGI(kTag, "no OpenSky credentials configured, polling anonymously every %lu ms",
            static_cast<unsigned long>(g_poll_interval_ms));
  }

  if (lvgl_port_lock(0)) {
    lv_obj_t *radar_screen = lv_screen_active();
    settings_view.init(radar_screen);
    ui::build_radar_screen(radar, plane_table, status_icon, settings_view.screen());
    radar.set_range_km(kRadarRangeKm);
    lv_timer_create(dead_reckon_timer_cb, kDeadReckonIntervalMs, nullptr);
    lvgl_port_unlock();
  } else {
    ESP_LOGE(kTag, "Failed to lock LVGL for UI setup");
  }

  aircraft_types.start();

  ESP_ERROR_CHECK(wifi_station::connect(config.wifi_ssid, config.wifi_password));

  xTaskCreate(opensky_poll_task, "opensky_poll", 8192, nullptr, tskIDLE_PRIORITY + 1, nullptr);
}
