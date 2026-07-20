#include "airport_client.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "geo.hpp"
#include "http_fetch.hpp"
#include "json_util.hpp"

namespace {
constexpr char kTag[] = "airports";

// zoom=10 is what makes the API return more than just the major hubs; at the
// default zoom the KC-area query returns only MCI and drops MKC.
constexpr char kUrlBase[] = "https://aviationweather.gov/api/data/airport";

// The API tags each entry with a priority tier: 1 = major hub ... 9 =
// hospital helipad. MKC (a towered downtown airport) is 5; the private grass
// strips and helipads around it are 6+. Keep the towered/public tiers only
// -- showing every helipad and grass strip cluttered the scope.
constexpr int kMaxPriority = 5;
}  // namespace

esp_err_t airport_client::fetch_airports(float home_lat_deg, float home_lon_deg, float range_km,
                                         std::vector<Airport> &out_airports) {
  out_airports.clear();

  const float lat_margin = range_km / 111.0f;
  const float lon_margin = range_km / (111.0f * std::cos(home_lat_deg * geo::kDegToRad));

  char url[256];
  const int url_len =
      std::snprintf(url, sizeof(url), "%s?bbox=%.4f,%.4f,%.4f,%.4f&format=json&zoom=10", kUrlBase,
                    home_lat_deg - lat_margin, home_lon_deg - lon_margin, home_lat_deg + lat_margin,
                    home_lon_deg + lon_margin);
  ESP_RETURN_ON_FALSE(url_len >= 0 && static_cast<size_t>(url_len) < sizeof(url), ESP_ERR_INVALID_SIZE,
                      kTag, "airports query URL truncated");

  ESP_LOGI(kTag, "requesting airports: %s", url);
  std::string response_body;
  ESP_RETURN_ON_ERROR(http_fetch(url, nullptr, nullptr, nullptr, response_body), kTag,
                      "airports request");

  const CJsonPtr root = cjson_parse(response_body.c_str());
  ESP_RETURN_ON_FALSE(root != nullptr, ESP_FAIL, kTag, "failed to parse airports response");

  const cJSON *airport = nullptr;
  cJSON_ArrayForEach(airport, root.get()) {
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(airport, "type");
    const cJSON *icao_id = cJSON_GetObjectItemCaseSensitive(airport, "icaoId");
    const cJSON *iata_id = cJSON_GetObjectItemCaseSensitive(airport, "iataId");
    const cJSON *priority = cJSON_GetObjectItemCaseSensitive(airport, "priority");
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(airport, "lat");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(airport, "lon");

    // "ARP" = airport (vs "HEL" heliports); no ICAO id means an unlisted
    // private strip.
    if (!cJSON_IsString(type) || std::strcmp(type->valuestring, "ARP") != 0) {
      continue;
    }
    if (!cJSON_IsString(icao_id) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon)) {
      continue;
    }
    if (cJSON_IsString(priority) && std::atoi(priority->valuestring) > kMaxPriority) {
      continue;
    }

    const geo::EastNorth position =
        geo::east_north_km(home_lat_deg, home_lon_deg, static_cast<float>(lat->valuedouble),
                           static_cast<float>(lon->valuedouble));

    Airport entry;
    entry.ident = cJSON_IsString(iata_id) ? iata_id->valuestring : icao_id->valuestring;
    entry.east_km = position.east_km;
    entry.north_km = position.north_km;
    out_airports.push_back(std::move(entry));
  }

  ESP_LOGI(kTag, "parsed %zu airports", out_airports.size());
  return ESP_OK;
}
