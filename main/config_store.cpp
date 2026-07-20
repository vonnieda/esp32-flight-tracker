#include "config_store.hpp"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "config_store";
constexpr char kNamespace[] = "ftcfg";

// RAII wrapper so nvs_close() always runs on scope exit, letting load()/
// save()/clear() bail out early via ESP_RETURN_ON_ERROR without having to
// thread "did an earlier step already fail" state through to a manual close.
class NvsHandle {
 public:
  NvsHandle(const char *name, nvs_open_mode_t mode) { open_result_ = nvs_open(name, mode, &handle_); }
  ~NvsHandle() {
    if (open_result_ == ESP_OK) nvs_close(handle_);
  }
  NvsHandle(const NvsHandle &) = delete;
  NvsHandle &operator=(const NvsHandle &) = delete;

  esp_err_t open_result() const { return open_result_; }
  nvs_handle_t get() const { return handle_; }

 private:
  nvs_handle_t handle_ = 0;
  esp_err_t open_result_ = ESP_FAIL;
};

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
  NvsHandle handle(kNamespace, NVS_READONLY);
  if (handle.open_result() != ESP_OK) {
    return false;  // Namespace doesn't exist yet: never configured.
  }

  Config loaded;
  const bool ok = 
    get_string(handle.get(), "ssid", loaded.wifi_ssid) == ESP_OK &&
    get_string(handle.get(), "pass", loaded.wifi_password) == ESP_OK &&
    get_string(handle.get(), "os_id", loaded.opensky_client_id)  == ESP_OK &&
    get_string(handle.get(), "os_secret", loaded.opensky_client_secret) == ESP_OK &&
    get_float(handle.get(), "lat", loaded.home_latitude_deg) == ESP_OK &&
    get_float(handle.get(), "lon", loaded.home_longitude_deg) == ESP_OK;

  if (!ok) {
    ESP_LOGW(kTag, "stored config incomplete, treating as unconfigured");
    return false;
  }

  out = std::move(loaded);
  return true;
}

esp_err_t config_store::save(const Config &config) {
  NvsHandle handle(kNamespace, NVS_READWRITE);
  ESP_RETURN_ON_ERROR(handle.open_result(), kTag, "open");

  ESP_RETURN_ON_ERROR(nvs_set_str(handle.get(), "ssid", config.wifi_ssid.c_str()), kTag, "ssid");
  ESP_RETURN_ON_ERROR(nvs_set_str(handle.get(), "pass", config.wifi_password.c_str()), kTag, "pass");
  ESP_RETURN_ON_ERROR(nvs_set_str(handle.get(), "os_id", config.opensky_client_id.c_str()), kTag,
                      "os_id");
  ESP_RETURN_ON_ERROR(nvs_set_str(handle.get(), "os_secret", config.opensky_client_secret.c_str()),
                      kTag, "os_secret");
  ESP_RETURN_ON_ERROR(nvs_set_blob(handle.get(), "lat", &config.home_latitude_deg, sizeof(float)),
                      kTag, "lat");
  ESP_RETURN_ON_ERROR(nvs_set_blob(handle.get(), "lon", &config.home_longitude_deg, sizeof(float)),
                      kTag, "lon");
  return nvs_commit(handle.get());
}

esp_err_t config_store::clear() {
  NvsHandle handle(kNamespace, NVS_READWRITE);
  ESP_RETURN_ON_ERROR(handle.open_result(), kTag, "open");
  ESP_RETURN_ON_ERROR(nvs_erase_all(handle.get()), kTag, "erase");
  return nvs_commit(handle.get());
}
