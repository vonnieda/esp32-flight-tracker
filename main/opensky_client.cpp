#include "opensky_client.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "geo.hpp"
#include "secrets_config.hpp"

namespace {
constexpr char kTag[] = "opensky";
constexpr char kTokenUrl[] =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
constexpr char kStatesUrlBase[] = "https://opensky-network.org/api/states/all";
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// Refresh a bit before the token actually expires to avoid races.
constexpr int64_t kExpiryMarginUs = 30LL * 1000 * 1000;

esp_err_t http_append_body_handler(esp_http_client_event_t *evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    auto *body = static_cast<std::string *>(evt->user_data);
    body->append(static_cast<const char *>(evt->data), evt->data_len);
  }
  return ESP_OK;
}

// Trims the trailing spaces OpenSky pads callsigns with.
std::string trim_trailing_spaces(const std::string &s) {
  size_t end = s.size();
  while (end > 0 && s[end - 1] == ' ') {
    --end;
  }
  return s.substr(0, end);
}
}  // namespace

esp_err_t OpenSkyClient::fetch_token() {
  std::string response_body;

  esp_http_client_config_t config{};
  config.url = kTokenUrl;
  config.method = HTTP_METHOD_POST;
  config.event_handler = http_append_body_handler;
  config.user_data = &response_body;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 10000;
  config.buffer_size = 4096;
  config.buffer_size_tx = 4096;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  ESP_RETURN_ON_FALSE(client != nullptr, ESP_FAIL, kTag, "failed to init http client");

  char post_field[256];
  std::snprintf(post_field, sizeof(post_field),
               "grant_type=client_credentials&client_id=%s&client_secret=%s",
               secrets::kOpenSkyClientId, secrets::kOpenSkyClientSecret);
  esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
  esp_http_client_set_post_field(client, post_field, static_cast<int>(std::strlen(post_field)));

  ESP_LOGI(kTag, "requesting token from %s", kTokenUrl);
  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200) {
    ESP_LOGE(kTag, "token request failed (err=%s, status=%d, body=%s)", esp_err_to_name(err),
             status, response_body.c_str());
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(response_body.c_str());
  ESP_RETURN_ON_FALSE(root != nullptr, ESP_FAIL, kTag, "failed to parse token response");

  const cJSON *access_token = cJSON_GetObjectItemCaseSensitive(root, "access_token");
  const cJSON *expires_in = cJSON_GetObjectItemCaseSensitive(root, "expires_in");
  if (!cJSON_IsString(access_token) || !cJSON_IsNumber(expires_in)) {
    cJSON_Delete(root);
    ESP_LOGE(kTag, "token response missing fields");
    return ESP_FAIL;
  }

  access_token_ = access_token->valuestring;
  token_expires_at_us_ = esp_timer_get_time() +
                        static_cast<int64_t>(expires_in->valuedouble) * 1000000LL -
                        kExpiryMarginUs;

  ESP_LOGI(kTag, "got token (%zu bytes), expires in %.0fs", access_token_.size(),
          expires_in->valuedouble);

  cJSON_Delete(root);
  return ESP_OK;
}

esp_err_t OpenSkyClient::ensure_token() {
  if (!access_token_.empty() && esp_timer_get_time() < token_expires_at_us_) {
    return ESP_OK;
  }
  return fetch_token();
}

esp_err_t OpenSkyClient::fetch_contacts(float home_lat_deg, float home_lon_deg, float range_km,
                                        std::vector<Contact> &out_contacts) {
  out_contacts.clear();

  ESP_RETURN_ON_ERROR(ensure_token(), kTag, "ensure token");

  const float lat_margin = range_km / 111.0f;
  const float lon_margin = range_km / (111.0f * std::cos(home_lat_deg * kDegToRad));

  char url[256];
  std::snprintf(url, sizeof(url), "%s?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f", kStatesUrlBase,
               home_lat_deg - lat_margin, home_lon_deg - lon_margin, home_lat_deg + lat_margin,
               home_lon_deg + lon_margin);

  std::string response_body;

  esp_http_client_config_t config{};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.event_handler = http_append_body_handler;
  config.user_data = &response_body;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 15000;
  config.buffer_size = 4096;
  config.buffer_size_tx = 4096;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  ESP_RETURN_ON_FALSE(client != nullptr, ESP_FAIL, kTag, "failed to init http client");

  const std::string auth_header = "Bearer " + access_token_;
  esp_http_client_set_header(client, "Authorization", auth_header.c_str());

  ESP_LOGI(kTag, "requesting states: %s", url);
  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200) {
    ESP_LOGE(kTag, "states request failed (err=%s, status=%d, body=%s)", esp_err_to_name(err),
             status, response_body.c_str());
    return ESP_FAIL;
  }
  ESP_LOGI(kTag, "states response: %zu bytes", response_body.size());

  cJSON *root = cJSON_Parse(response_body.c_str());
  ESP_RETURN_ON_FALSE(root != nullptr, ESP_FAIL, kTag, "failed to parse states response");

  const cJSON *states = cJSON_GetObjectItemCaseSensitive(root, "states");
  if (!cJSON_IsArray(states)) {
    // No traffic in range; OpenSky returns states: null in this case.
    ESP_LOGI(kTag, "no traffic in range");
    cJSON_Delete(root);
    return ESP_OK;
  }

  const cJSON *state = nullptr;
  cJSON_ArrayForEach(state, states) {
    if (!cJSON_IsArray(state)) {
      continue;
    }
    const cJSON *callsign_item = cJSON_GetArrayItem(state, 1);
    const cJSON *longitude_item = cJSON_GetArrayItem(state, 5);
    const cJSON *latitude_item = cJSON_GetArrayItem(state, 6);
    const cJSON *baro_altitude_item = cJSON_GetArrayItem(state, 7);
    const cJSON *on_ground_item = cJSON_GetArrayItem(state, 8);
    const cJSON *true_track_item = cJSON_GetArrayItem(state, 10);

    if (!cJSON_IsNumber(longitude_item) || !cJSON_IsNumber(latitude_item)) {
      continue;  // Position unknown; nothing to plot.
    }
    if (cJSON_IsTrue(on_ground_item)) {
      continue;  // Not interesting for an overhead radar.
    }

    const auto latitude = static_cast<float>(latitude_item->valuedouble);
    const auto longitude = static_cast<float>(longitude_item->valuedouble);

    Contact contact;
    contact.callsign =
        cJSON_IsString(callsign_item) ? trim_trailing_spaces(callsign_item->valuestring) : "?";
    contact.distance_km = geo::distance_km(home_lat_deg, home_lon_deg, latitude, longitude);
    contact.bearing_deg = geo::bearing_deg(home_lat_deg, home_lon_deg, latitude, longitude);
    contact.altitude_ft = cJSON_IsNumber(baro_altitude_item)
                             ? static_cast<float>(baro_altitude_item->valuedouble) * 3.28084f
                             : 0.0f;
    contact.track_deg =
        cJSON_IsNumber(true_track_item) ? static_cast<float>(true_track_item->valuedouble) : 0.0f;

    out_contacts.push_back(std::move(contact));
  }

  ESP_LOGI(kTag, "parsed %zu contacts", out_contacts.size());

  cJSON_Delete(root);
  return ESP_OK;
}
