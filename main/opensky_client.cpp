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

namespace {
constexpr char kTag[] = "opensky";
constexpr char kTokenUrl[] =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
constexpr char kStatesUrlBase[] = "https://opensky-network.org/api/states/all";

// Refresh a bit before the token actually expires to avoid races.
constexpr int64_t kExpiryMarginUs = 30LL * 1000 * 1000;

esp_err_t http_append_body_handler(esp_http_client_event_t *evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    auto *body = static_cast<std::string *>(evt->user_data);
    body->append(static_cast<const char *>(evt->data), evt->data_len);
  }
  return ESP_OK;
}

// Shared plumbing for both OpenSky endpoints: HTTPS via the certificate
// bundle, the response body collected into out_body, and any non-200 status
// treated as an error. A non-null post_body makes the request a POST.
esp_err_t http_request(const char *url, const char *header_name, const char *header_value,
                       const char *post_body, std::string &out_body) {
  esp_http_client_config_t config{};
  config.url = url;
  config.method = post_body != nullptr ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  config.event_handler = http_append_body_handler;
  config.user_data = &out_body;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 15000;
  config.buffer_size = 4096;
  config.buffer_size_tx = 4096;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  ESP_RETURN_ON_FALSE(client != nullptr, ESP_FAIL, kTag, "failed to init http client");

  esp_http_client_set_header(client, header_name, header_value);
  if (post_body != nullptr) {
    esp_http_client_set_post_field(client, post_body, static_cast<int>(std::strlen(post_body)));
  }

  const esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200) {
    ESP_LOGE(kTag, "request to %s failed (err=%s, status=%d, body=%s)", url, esp_err_to_name(err),
             status, out_body.c_str());
    return ESP_FAIL;
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

void OpenSkyClient::set_credentials(std::string client_id, std::string client_secret) {
  client_id_ = std::move(client_id);
  client_secret_ = std::move(client_secret);
}

esp_err_t OpenSkyClient::fetch_token() {
  const std::string post_body =
      "grant_type=client_credentials&client_id=" + client_id_ + "&client_secret=" + client_secret_;

  ESP_LOGI(kTag, "requesting token from %s", kTokenUrl);
  std::string response_body;
  ESP_RETURN_ON_ERROR(http_request(kTokenUrl, "Content-Type", "application/x-www-form-urlencoded",
                                   post_body.c_str(), response_body),
                      kTag, "token request");

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

bool OpenSkyClient::has_valid_token() const {
  return !access_token_.empty() && esp_timer_get_time() < token_expires_at_us_;
}

esp_err_t OpenSkyClient::ensure_token() {
  if (has_valid_token()) {
    return ESP_OK;
  }
  return fetch_token();
}

esp_err_t OpenSkyClient::fetch_contacts(float home_lat_deg, float home_lon_deg, float range_km,
                                        std::vector<Contact> &out_contacts) {
  out_contacts.clear();

  ESP_RETURN_ON_ERROR(ensure_token(), kTag, "ensure token");

  const float lat_margin = range_km / 111.0f;
  const float lon_margin = range_km / (111.0f * std::cos(home_lat_deg * geo::kDegToRad));

  char url[256];
  std::snprintf(url, sizeof(url), "%s?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f", kStatesUrlBase,
               home_lat_deg - lat_margin, home_lon_deg - lon_margin, home_lat_deg + lat_margin,
               home_lon_deg + lon_margin);

  const std::string auth_header = "Bearer " + access_token_;

  ESP_LOGI(kTag, "requesting states: %s", url);
  std::string response_body;
  ESP_RETURN_ON_ERROR(http_request(url, "Authorization", auth_header.c_str(), nullptr,
                                   response_body),
                      kTag, "states request");
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
    const cJSON *velocity_item = cJSON_GetArrayItem(state, 9);
    const cJSON *true_track_item = cJSON_GetArrayItem(state, 10);

    if (!cJSON_IsNumber(longitude_item) || !cJSON_IsNumber(latitude_item)) {
      continue;  // Position unknown; nothing to plot.
    }
    if (cJSON_IsTrue(on_ground_item)) {
      continue;  // Not interesting for an overhead radar.
    }

    const auto latitude = static_cast<float>(latitude_item->valuedouble);
    const auto longitude = static_cast<float>(longitude_item->valuedouble);
    const geo::EastNorth position = geo::east_north_km(home_lat_deg, home_lon_deg, latitude,
                                                       longitude);

    Contact contact;
    contact.callsign =
        cJSON_IsString(callsign_item) ? trim_trailing_spaces(callsign_item->valuestring) : "?";
    contact.east_km = position.east_km;
    contact.north_km = position.north_km;
    contact.altitude_ft = cJSON_IsNumber(baro_altitude_item)
                             ? static_cast<float>(baro_altitude_item->valuedouble) * 3.28084f
                             : 0.0f;
    contact.track_deg =
        cJSON_IsNumber(true_track_item) ? static_cast<float>(true_track_item->valuedouble) : 0.0f;
    contact.ground_speed_mps =
        cJSON_IsNumber(velocity_item) ? static_cast<float>(velocity_item->valuedouble) : 0.0f;

    out_contacts.push_back(std::move(contact));
  }

  ESP_LOGI(kTag, "parsed %zu contacts", out_contacts.size());

  cJSON_Delete(root);
  return ESP_OK;
}
