#include "map_client.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "cJSON.h"
#include "esp_log.h"
#include "geo.hpp"
#include "http_fetch.hpp"

namespace {
constexpr char kTag[] = "map_client";

// jsdelivr, not raw.githubusercontent.com: raw.githubusercontent.com
// aggressively rate-limits (429s) and its ToS explicitly disallows
// programmatic/repeated access, which every device boot would trip. jsdelivr
// is a CDN built for exactly this -- serving files out of a GitHub repo to
// many clients -- and returns identical bytes.
constexpr char kNeBase[] = "https://cdn.jsdelivr.net/gh/nvkelso/natural-earth-vector@master/geojson/";
// 1:110m is Natural Earth's whole-world-overview resolution: coastline,
// country and state borders together are ~600KB total, vs. 10s of MB for the
// 1:10m data tools/make_map.py fetched -- fine to hold in PSRAM and parse
// whole with cJSON, no streaming parser needed. Country/state borders are
// mostly straight lines anyway, so the coarser resolution costs little; it's
// coastlines that would look blockiest, but that's an acceptable trade for
// not needing a multi-MB download on every boot.
constexpr const char *kSourceFiles[] = {
    "ne_110m_coastline.geojson",
    "ne_110m_admin_0_boundary_lines_land.geojson",
    "ne_110m_admin_1_states_provinces_lines.geojson",
};

constexpr float kKmPerDegLat = 110.574f;
constexpr float kKmPerDegLon = 111.320f;  // at equator; scaled by cos(lat).

// Point budget for the simplified outline; tolerance is raised until the
// output fits, same as tools/make_map.py's --max-points.
constexpr size_t kMaxPoints = 4000;

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

std::vector<LatLon> parse_ring(const cJSON *ring) {
  std::vector<LatLon> pts;
  const cJSON *pt = nullptr;
  cJSON_ArrayForEach(pt, ring) {
    const cJSON *lon_item = cJSON_GetArrayItem(pt, 0);
    const cJSON *lat_item = cJSON_GetArrayItem(pt, 1);
    if (cJSON_IsNumber(lon_item) && cJSON_IsNumber(lat_item)) {
      pts.push_back({static_cast<float>(lat_item->valuedouble), static_cast<float>(lon_item->valuedouble)});
    }
  }
  return pts;
}

// Yields lists of points from any common GeoJSON geometry.
void iter_polylines(const cJSON *geometry, std::vector<std::vector<LatLon>> &out) {
  if (geometry == nullptr) return;
  const cJSON *type_item = cJSON_GetObjectItemCaseSensitive(geometry, "type");
  if (!cJSON_IsString(type_item)) return;
  const char *type = type_item->valuestring;
  const cJSON *coords = cJSON_GetObjectItemCaseSensitive(geometry, "coordinates");

  if (std::strcmp(type, "LineString") == 0) {
    out.push_back(parse_ring(coords));
  } else if (std::strcmp(type, "MultiLineString") == 0 || std::strcmp(type, "Polygon") == 0) {
    const cJSON *ring = nullptr;
    cJSON_ArrayForEach(ring, coords) out.push_back(parse_ring(ring));
  } else if (std::strcmp(type, "MultiPolygon") == 0) {
    const cJSON *poly = nullptr;
    cJSON_ArrayForEach(poly, coords) {
      const cJSON *ring = nullptr;
      cJSON_ArrayForEach(ring, poly) out.push_back(parse_ring(ring));
    }
  } else if (std::strcmp(type, "GeometryCollection") == 0) {
    const cJSON *g = nullptr;
    cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(geometry, "geometries")) {
      iter_polylines(g, out);
    }
  }
}

// Fetches one source file and appends its clipped polylines to out_clipped.
// Failures (network, parse) are logged and swallowed -- best-effort across
// the three sources, so a single flaky fetch doesn't blank the whole map.
void fetch_and_clip(const char *url, float lat0, float lon0, float dlat, float dlon,
                    std::vector<std::vector<LatLon>> &out_clipped) {
  std::string body;
  if (http_fetch(url, nullptr, nullptr, nullptr, body) != ESP_OK) {
    ESP_LOGW(kTag, "fetch failed: %s", url);
    return;
  }
  cJSON *root = cJSON_Parse(body.c_str());
  body.clear();
  body.shrink_to_fit();
  if (root == nullptr) {
    ESP_LOGW(kTag, "parse failed: %s", url);
    return;
  }

  const cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
  std::vector<std::vector<LatLon>> raw;
  if (cJSON_IsArray(features)) {
    const cJSON *ft = nullptr;
    cJSON_ArrayForEach(ft, features) {
      const cJSON *geom = cJSON_GetObjectItemCaseSensitive(ft, "geometry");
      iter_polylines(geom != nullptr ? geom : ft, raw);
    }
  } else {
    iter_polylines(root, raw);
  }
  cJSON_Delete(root);

  for (const auto &pl : raw) clip_polyline(pl, lat0, lon0, dlat, dlon, out_clipped);
}
}  // namespace

esp_err_t map_client::fetch_outline(float home_lat_deg, float home_lon_deg, float range_km,
                                    std::vector<float> &out_outline) {
  out_outline.clear();

  if (std::fabs(home_lat_deg) > 85.0f) {
    ESP_LOGE(kTag, "latitudes beyond +/-85 are not supported");
    return ESP_ERR_INVALID_ARG;
  }
  const float coslat = std::cos(home_lat_deg * geo::kDegToRad);
  // 1.3x margin so the map still covers a later range increase.
  const float dlat = range_km * 1.3f / kKmPerDegLat;
  const float dlon = range_km * 1.3f / (kKmPerDegLon * coslat);
  if (std::fabs(home_lon_deg) + dlon > 180.0f) {
    ESP_LOGE(kTag, "bounding box would cross the antimeridian - not supported");
    return ESP_ERR_INVALID_ARG;
  }

  std::vector<std::vector<LatLon>> clipped;
  for (const char *name : kSourceFiles) {
    const std::string url = std::string(kNeBase) + name;
    fetch_and_clip(url.c_str(), home_lat_deg, home_lon_deg, dlat, dlon, clipped);
  }
  if (clipped.empty()) {
    ESP_LOGW(kTag, "no map lines found for this location/range");
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
