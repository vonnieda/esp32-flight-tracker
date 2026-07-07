#include "config_store.hpp"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "config_store";
constexpr char kNamespace[] = "ftcfg";

esp_err_t get_string(nvs_handle_t handle, const char *key, std::string &out) {
  size_t len = 0;
  esp_err_t err = nvs_get_str(handle, key, nullptr, &len);
  if (err != ESP_OK) {
    return err;
  }
  out.resize(len);  // len includes the null terminator.
  err = nvs_get_str(handle, key, out.data(), &len);
  if (err == ESP_OK && !out.empty()) {
    out.resize(len - 1);
  }
  return err;
}

esp_err_t get_float(nvs_handle_t handle, const char *key, float &out) {
  size_t len = sizeof(out);
  return nvs_get_blob(handle, key, &out, &len);
}
}  // namespace

esp_err_t config_store::init() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), kTag, "nvs erase");
    err = nvs_flash_init();
  }
  return err;
}

bool config_store::load(Config &out) {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;  // Namespace doesn't exist yet: never configured.
  }

  Config loaded;
  const bool ok = get_string(handle, "ssid", loaded.wifi_ssid) == ESP_OK &&
                  get_string(handle, "pass", loaded.wifi_password) == ESP_OK &&
                  get_string(handle, "os_id", loaded.opensky_client_id) == ESP_OK &&
                  get_string(handle, "os_secret", loaded.opensky_client_secret) == ESP_OK &&
                  get_float(handle, "lat", loaded.home_latitude_deg) == ESP_OK &&
                  get_float(handle, "lon", loaded.home_longitude_deg) == ESP_OK;
  nvs_close(handle);

  if (!ok) {
    ESP_LOGW(kTag, "stored config incomplete, treating as unconfigured");
    return false;
  }

  out = std::move(loaded);
  return true;
}

esp_err_t config_store::save(const Config &config) {
  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &handle), kTag, "open");

  esp_err_t err = ESP_OK;
  err = err == ESP_OK ? nvs_set_str(handle, "ssid", config.wifi_ssid.c_str()) : err;
  err = err == ESP_OK ? nvs_set_str(handle, "pass", config.wifi_password.c_str()) : err;
  err = err == ESP_OK ? nvs_set_str(handle, "os_id", config.opensky_client_id.c_str()) : err;
  err = err == ESP_OK ? nvs_set_str(handle, "os_secret", config.opensky_client_secret.c_str())
                     : err;
  err = err == ESP_OK
          ? nvs_set_blob(handle, "lat", &config.home_latitude_deg, sizeof(float))
          : err;
  err = err == ESP_OK
          ? nvs_set_blob(handle, "lon", &config.home_longitude_deg, sizeof(float))
          : err;
  err = err == ESP_OK ? nvs_commit(handle) : err;

  nvs_close(handle);
  return err;
}
