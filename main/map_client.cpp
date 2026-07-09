#include "map_client.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>

#include "cJSON.h"
#include "esp_log.h"
#include "geo.hpp"
#include "http_fetch.hpp"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "map_client";

constexpr char kOverpassUrl[] = "https://overpass-api.de/api/interpreter";
// Comfortably under http_fetch's fixed 15s socket timeout, so a query that's
// about to time out server-side fails fast instead of us hitting the socket
// timeout first and losing the (already-computed) partial result.
constexpr int kOverpassTimeoutSec = 10;

constexpr float kKmPerDegLat = 110.574f;
constexpr float kKmPerDegLon = 111.320f;  // at equator; scaled by cos(lat).

// Point budget for the simplified outline; tolerance is raised until the
// output fits, same as tools/make_map.py's --max-points.
constexpr size_t kMaxPoints = 4000;

// Dedicated NVS partition (see partitions.csv) for caching fetched/simplified
// outlines, so a reboot at the same home location doesn't have to re-query
// Overpass -- same rationale as aircraft_type_client's "actypes" partition.
constexpr char kCachePartition[] = "mapcache";
constexpr char kCacheNamespace[] = "outlines";

struct LatLon {
  float lat;
  float lon;
};

struct XY {
  float x;
  float y;
};

bool inside_bbox(const LatLon &p, float lat0, float lon0, float dlat, float dlon) {
  return std::fabs(p.lat - lat0) <= dlat && std::fabs(p.lon - lon0) <= dlon;
}

// Splits a polyline into runs that touch the bbox around (lat0, lon0). A
// point is kept if it or either neighbor is inside the box, so lines
// crossing the box edge aren't cut short.
void clip_polyline(const std::vector<LatLon> &pts, float lat0, float lon0, float dlat, float dlon,
                   std::vector<std::vector<LatLon>> &out_runs) {
  const size_t n = pts.size();
  if (n < 2) return;
  std::vector<bool> flags(n);
  for (size_t i = 0; i < n; i++) flags[i] = inside_bbox(pts[i], lat0, lon0, dlat, dlon);

  std::vector<LatLon> cur;
  for (size_t i = 0; i < n; i++) {
    const bool keep = flags[i] || (i > 0 && flags[i - 1]) || (i + 1 < n && flags[i + 1]);
    if (keep) {
      cur.push_back(pts[i]);
    } else if (!cur.empty()) {
      if (cur.size() >= 2) out_runs.push_back(cur);
      cur.clear();
    }
  }
  if (cur.size() >= 2) out_runs.push_back(cur);
}

// Douglas-Peucker on (x, y) points, iterative to avoid recursion limits.
std::vector<XY> dp_simplify(const std::vector<XY> &pts, float tol) {
  const int n = static_cast<int>(pts.size());
  if (n < 3) return pts;
  std::vector<bool> keep(n, false);
  keep[0] = keep[n - 1] = true;
  std::vector<std::pair<int, int>> stack;
  stack.push_back({0, n - 1});
  const float tol2 = tol * tol;
  while (!stack.empty()) {
    const auto [a, b] = stack.back();
    stack.pop_back();
    const float ax = pts[a].x, ay = pts[a].y;
    const float bx = pts[b].x, by = pts[b].y;
    const float dx = bx - ax, dy = by - ay;
    const float seg2 = dx * dx + dy * dy;
    float dmax = -1.0f;
    int imax = -1;
    for (int i = a + 1; i < b; i++) {
      const float px = pts[i].x, py = pts[i].y;
      float d2;
      if (seg2 == 0.0f) {
        d2 = (px - ax) * (px - ax) + (py - ay) * (py - ay);
      } else {
        float t = ((px - ax) * dx + (py - ay) * dy) / seg2;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const float ex = ax + t * dx, ey = ay + t * dy;
        d2 = (px - ex) * (px - ex) + (py - ey) * (py - ey);
      }
      if (d2 > dmax) {
        dmax = d2;
        imax = i;
      }
    }
    if (dmax > tol2) {
      keep[imax] = true;
      stack.push_back({a, imax});
      stack.push_back({imax, b});
    }
  }
  std::vector<XY> out;
  out.reserve(n);
  for (int i = 0; i < n; i++) {
    if (keep[i]) out.push_back(pts[i]);
  }
  return out;
}

// Clip -> project lon by cos(lat) -> simplify -> back to (lat, lon).
std::vector<std::vector<LatLon>> build(const std::vector<std::vector<LatLon>> &polylines,
                                       float coslat, float tol) {
  std::vector<std::vector<LatLon>> out;
  for (const auto &pl : polylines) {
    std::vector<XY> scaled;
    scaled.reserve(pl.size());
    for (const auto &p : pl) scaled.push_back({p.lon * coslat, p.lat});
    const std::vector<XY> simplified = dp_simplify(scaled, tol);
    if (simplified.size() < 2) continue;
    std::vector<LatLon> line;
    line.reserve(simplified.size());
    for (const auto &xy : simplified) line.push_back({xy.y, xy.x / coslat});
    out.push_back(std::move(line));
  }
  return out;
}

// Overpass's "out geom;" gives each way a "geometry": [{"lat":.., "lon":..},
// ...] array -- simpler to walk than nested GeoJSON coordinate arrays since
// there's no Polygon/MultiPolygon ring nesting to handle.
std::vector<LatLon> parse_geometry(const cJSON *geometry) {
  std::vector<LatLon> pts;
  const cJSON *pt = nullptr;
  cJSON_ArrayForEach(pt, geometry) {
    const cJSON *lat_item = cJSON_GetObjectItemCaseSensitive(pt, "lat");
    const cJSON *lon_item = cJSON_GetObjectItemCaseSensitive(pt, "lon");
    if (cJSON_IsNumber(lat_item) && cJSON_IsNumber(lon_item)) {
      pts.push_back({static_cast<float>(lat_item->valuedouble), static_cast<float>(lon_item->valuedouble)});
    }
  }
  return pts;
}

// POSTs an Overpass QL query and appends the clipped geometry of every
// returned way to out_clipped. Failures (network, parse) are logged and
// swallowed -- the caller treats "nothing came back" as the only failure.
void fetch_and_clip(const std::string &query, float lat0, float lon0, float dlat, float dlon,
                    std::vector<std::vector<LatLon>> &out_clipped) {
  std::string body;
  if (http_fetch(kOverpassUrl, nullptr, nullptr, query.c_str(), body) != ESP_OK) {
    ESP_LOGW(kTag, "overpass fetch failed");
    return;
  }
  cJSON *root = cJSON_Parse(body.c_str());
  body.clear();
  body.shrink_to_fit();
  if (root == nullptr) {
    ESP_LOGW(kTag, "overpass response parse failed");
    return;
  }

  std::vector<std::vector<LatLon>> raw;
  const cJSON *element = nullptr;
  cJSON_ArrayForEach(element, cJSON_GetObjectItemCaseSensitive(root, "elements")) {
    const cJSON *geometry = cJSON_GetObjectItemCaseSensitive(element, "geometry");
    if (cJSON_IsArray(geometry)) {
      raw.push_back(parse_geometry(geometry));
    }
  }
  cJSON_Delete(root);

  for (const auto &pl : raw) clip_polyline(pl, lat0, lon0, dlat, dlon, out_clipped);
}

std::string build_admin_query(float lat, float lon, float radius_m) {
  char buf[512];
  // way(r) alone would pull every member way of a matched relation -- for a
  // country or state boundary that's the *entire* national/state border, not
  // just the segment near home (this crashed the device: multi-MB response,
  // OOM in http_fetch's response-body string). Re-applying (around:...) to
  // the way selection clips it back down to just the nearby segment.
  std::snprintf(buf, sizeof(buf),
               "[out:json][timeout:%d];\n"
               "(\n"
               "  relation[\"boundary\"=\"administrative\"][\"admin_level\"=\"2\"]"
               "(around:%.0f,%.6f,%.6f);\n"
               "  relation[\"boundary\"=\"administrative\"][\"admin_level\"=\"4\"]"
               "(around:%.0f,%.6f,%.6f);\n"
               ");\n"
               "way(r)(around:%.0f,%.6f,%.6f);\n"
               "out geom;",
               kOverpassTimeoutSec, radius_m, lat, lon, radius_m, lat, lon, radius_m, lat, lon);
  return buf;
}

// Validates the home location and derives the clip margins (in degrees, for
// clip_polyline) and query radius (in meters, for Overpass's "around" filter).
esp_err_t query_geometry(float home_lat_deg, float home_lon_deg, float range_km, float &coslat,
                         float &dlat, float &dlon, float &radius_m) {
  if (std::fabs(home_lat_deg) > 85.0f) {
    ESP_LOGE(kTag, "latitudes beyond +/-85 are not supported");
    return ESP_ERR_INVALID_ARG;
  }
  coslat = std::cos(home_lat_deg * geo::kDegToRad);
  // 1.3x margin so the map still covers a later range increase.
  dlat = range_km * 1.3f / kKmPerDegLat;
  dlon = range_km * 1.3f / (kKmPerDegLon * coslat);
  if (std::fabs(home_lon_deg) + dlon > 180.0f) {
    ESP_LOGE(kTag, "bounding box would cross the antimeridian - not supported");
    return ESP_ERR_INVALID_ARG;
  }
  radius_m = range_km * 1.3f * 1000.0f;
  return ESP_OK;
}

esp_err_t simplify_and_flatten(const std::vector<std::vector<LatLon>> &clipped, float coslat,
                               float range_km, std::vector<float> &out_outline) {
  if (clipped.empty()) {
    return ESP_FAIL;
  }
  // ~1 px on the 456 px radar disc at full radius, floor 0.002 deg.
  float tol = std::fmax(0.002f, 2.0f * range_km / 456.0f / 111.0f);
  std::vector<std::vector<LatLon>> lines;
  while (true) {
    lines = build(clipped, coslat, tol);
    size_t npts = 0;
    for (const auto &l : lines) npts += l.size();
    if (npts <= kMaxPoints || lines.empty()) break;
    tol *= 1.5f;
  }
  if (lines.empty()) {
    return ESP_FAIL;
  }

  size_t npts = 0;
  for (const auto &line : lines) {
    for (const auto &p : line) {
      out_outline.push_back(p.lat);
      out_outline.push_back(p.lon);
    }
    out_outline.push_back(std::numeric_limits<float>::quiet_NaN());
    out_outline.push_back(std::numeric_limits<float>::quiet_NaN());
    npts += line.size();
  }
  ESP_LOGI(kTag, "%zu polylines, %zu points", lines.size(), npts);
  return ESP_OK;
}

void ensure_cache_partition_ready() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;
  esp_err_t err = nvs_flash_init_partition(kCachePartition);
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase_partition(kCachePartition);
    err = nvs_flash_init_partition(kCachePartition);
  }
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "mapcache partition unavailable, outlines won't survive reboot (%s)",
             esp_err_to_name(err));
  }
}

// Cache is keyed by the home coordinates the outline was generated for; a
// location change is just a cache miss, no separate invalidation needed.
bool load_from_cache(float home_lat_deg, float home_lon_deg, std::vector<float> &out_outline) {
  ensure_cache_partition_ready();
  nvs_handle_t handle;
  if (nvs_open_from_partition(kCachePartition, kCacheNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }

  float cached_lat = 0.0f, cached_lon = 0.0f;
  size_t len = sizeof(float);
  bool ok = nvs_get_blob(handle, "lat", &cached_lat, &len) == ESP_OK;
  len = sizeof(float);
  ok = ok && nvs_get_blob(handle, "lon", &cached_lon, &len) == ESP_OK;
  ok = ok && cached_lat == home_lat_deg && cached_lon == home_lon_deg;

  if (ok) {
    len = 0;
    ok = nvs_get_blob(handle, "pts", nullptr, &len) == ESP_OK && len > 0;
  }
  if (ok) {
    out_outline.resize(len / sizeof(float));
    ok = nvs_get_blob(handle, "pts", out_outline.data(), &len) == ESP_OK;
  }
  nvs_close(handle);
  if (ok) {
    ESP_LOGI(kTag, "loaded %zu cached floats from flash", out_outline.size());
  }
  return ok;
}

void store_to_cache(float home_lat_deg, float home_lon_deg, const std::vector<float> &outline) {
  ensure_cache_partition_ready();
  nvs_handle_t handle;
  if (nvs_open_from_partition(kCachePartition, kCacheNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return;
  }
  nvs_set_blob(handle, "lat", &home_lat_deg, sizeof(float));
  nvs_set_blob(handle, "lon", &home_lon_deg, sizeof(float));
  nvs_set_blob(handle, "pts", outline.data(), outline.size() * sizeof(float));
  nvs_commit(handle);
  nvs_close(handle);
}
}  // namespace

esp_err_t map_client::fetch_admin_outline(float home_lat_deg, float home_lon_deg, float range_km,
                                          std::vector<float> &out_outline) {
  out_outline.clear();
  if (load_from_cache(home_lat_deg, home_lon_deg, out_outline)) {
    return ESP_OK;
  }

  float coslat, dlat, dlon, radius_m;
  const esp_err_t err = query_geometry(home_lat_deg, home_lon_deg, range_km, coslat, dlat, dlon, radius_m);
  if (err != ESP_OK) return err;

  std::vector<std::vector<LatLon>> clipped;
  fetch_and_clip(build_admin_query(home_lat_deg, home_lon_deg, radius_m), home_lat_deg, home_lon_deg,
                dlat, dlon, clipped);

  const esp_err_t simplify_err = simplify_and_flatten(clipped, coslat, range_km, out_outline);
  if (simplify_err != ESP_OK) {
    ESP_LOGW(kTag, "no lines found for this location/range");
    return simplify_err;
  }
  store_to_cache(home_lat_deg, home_lon_deg, out_outline);
  return ESP_OK;
}
