#include "opensky_client.hpp"

#include <cmath>
#include <cstdio>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "geo.hpp"
#include "http_fetch.hpp"

namespace {
constexpr char kTag[] = "opensky";
constexpr char kTokenUrl[] =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
constexpr char kStatesUrlBase[] = "https://opensky-network.org/api/states/all";

// Refresh a bit before the token actually expires to avoid races.
constexpr int64_t kExpiryMarginUs = 30LL * 1000 * 1000;

// OpenSky keeps returning an aircraft's last known state for a while after
// it stops transmitting (e.g. right after landing), with the old velocity
// still attached -- which made landed planes "fly" across the scope via dead
// reckoning and snap back every poll. Drop any state whose position or
// overall report is older than this relative to the response timestamp.
constexpr double kMaxStateAgeS = 60.0;

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

bool OpenSkyClient::is_authenticated() const {
  return !client_id_.empty() && !client_secret_.empty();
}

esp_err_t OpenSkyClient::fetch_token() {
  const std::string post_body =
      "grant_type=client_credentials&client_id=" + client_id_ + "&client_secret=" + client_secret_;

  ESP_LOGI(kTag, "requesting token from %s", kTokenUrl);
  std::string response_body;
  ESP_RETURN_ON_ERROR(http_fetch(kTokenUrl, "Content-Type", "application/x-www-form-urlencoded",
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

  // Without credentials, skip the OAuth2 dance entirely and hit the states
  // endpoint anonymously -- OpenSky allows this, just at a much lower daily
  // request quota (see main.cpp's poll interval).
  const bool authenticated = is_authenticated();
  if (authenticated) {
    ESP_RETURN_ON_ERROR(ensure_token(), kTag, "ensure token");
  }

  const float lat_margin = range_km / 111.0f;
  const float lon_margin = range_km / (111.0f * std::cos(home_lat_deg * geo::kDegToRad));

  char url[256];
  std::snprintf(url, sizeof(url), "%s?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f", kStatesUrlBase,
               home_lat_deg - lat_margin, home_lon_deg - lon_margin, home_lat_deg + lat_margin,
               home_lon_deg + lon_margin);

  const std::string auth_header = "Bearer " + access_token_;

  ESP_LOGI(kTag, "requesting states: %s", url);
  std::string response_body;
  ESP_RETURN_ON_ERROR(http_fetch(url, authenticated ? "Authorization" : nullptr,
                                 authenticated ? auth_header.c_str() : nullptr, nullptr,
                                 response_body),
                      kTag, "states request");
  ESP_LOGI(kTag, "states response: %zu bytes", response_body.size());

  cJSON *root = cJSON_Parse(response_body.c_str());
  ESP_RETURN_ON_FALSE(root != nullptr, ESP_FAIL, kTag, "failed to parse states response");

  const cJSON *response_time_item = cJSON_GetObjectItemCaseSensitive(root, "time");
  const double response_time =
      cJSON_IsNumber(response_time_item) ? response_time_item->valuedouble : 0.0;

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
    const cJSON *icao24_item = cJSON_GetArrayItem(state, 0);
    const cJSON *callsign_item = cJSON_GetArrayItem(state, 1);
    const cJSON *time_position_item = cJSON_GetArrayItem(state, 3);
    const cJSON *last_contact_item = cJSON_GetArrayItem(state, 4);
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
    if (response_time > 0.0 &&
        ((cJSON_IsNumber(time_position_item) &&
          response_time - time_position_item->valuedouble > kMaxStateAgeS) ||
         (cJSON_IsNumber(last_contact_item) &&
          response_time - last_contact_item->valuedouble > kMaxStateAgeS))) {
      continue;  // Stale last-known state; the aircraft likely landed.
    }

    const auto latitude = static_cast<float>(latitude_item->valuedouble);
    const auto longitude = static_cast<float>(longitude_item->valuedouble);
    const geo::EastNorth position = geo::east_north_km(home_lat_deg, home_lon_deg, latitude,
                                                       longitude);

    Contact contact;
    contact.icao24 = cJSON_IsString(icao24_item) ? icao24_item->valuestring : "";
    contact.callsign =
        cJSON_IsString(callsign_item) ? trim_trailing_spaces(callsign_item->valuestring) : "?";
    contact.east_km = position.east_km;
    contact.north_km = position.north_km;
    contact.altitude_ft = cJSON_IsNumber(baro_altitude_item)
                             ? static_cast<float>(baro_altitude_item->valuedouble) * 3.28084f
                             : NAN;
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
