#include "plane_table_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#include "plane_color.hpp"

namespace {
constexpr lv_color_t kColorBackground = LV_COLOR_MAKE(0x00, 0x00, 0x00);
constexpr lv_color_t kColorHeaderText = LV_COLOR_MAKE(0x00, 0x8f, 0x11);
constexpr lv_color_t kColorHeaderRule = LV_COLOR_MAKE(0x00, 0x4d, 0x14);
constexpr int kSpeedColWidth = 36;
constexpr int kDistanceColWidth = 38;
constexpr int kAltitudeColWidth = 40;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kMpsToKnots = 1.943844f;

// Above this, altitude is abbreviated to the nearest thousand feet with a
// "k" suffix (e.g. "27k") rather than the full value, to keep the column
// narrow.
constexpr float kAltitudeAbbreviateThresholdFt = 10000.0f;

// Caps the list to a screenful regardless of how many contacts the (wider)
// query radius returns; nearest-first sort means anything beyond this is
// the least relevant traffic anyway.
constexpr size_t kMaxTableRows = 11;

// Was 100 (matching RadarView's own tick so the two views drift in
// lockstep, still true at 1000). Re-rasterizing all 4 labels' text 10x/sec
// was a real FPS cost (see the FPS investigation) that cutting LVGL call
// *count* alone (see the recolor usage below) didn't fix -- only cutting
// how often the text gets redrawn did. Once a second is imperceptible for
// distances that only change over tens of seconds.
constexpr uint32_t kMotionTickMs = 1000;

lv_obj_t *make_row_container(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return row;
}
}  // namespace

void PlaneTableView::init(lv_obj_t *parent, int height) {
  list_area_ = lv_obj_create(parent);
  lv_obj_remove_style_all(list_area_);
  lv_obj_set_size(list_area_, LV_SIZE_CONTENT, height);
  lv_obj_set_flex_grow(list_area_, 1);
  lv_obj_set_style_bg_color(list_area_, kColorBackground, 0);
  lv_obj_set_style_bg_opa(list_area_, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(list_area_, 6, 0);
  lv_obj_set_style_pad_row(list_area_, 4, 0);
  lv_obj_set_flex_flow(list_area_, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *header = make_row_container(list_area_);
  lv_obj_set_style_border_color(header, kColorHeaderRule, 0);
  lv_obj_set_style_border_width(header, 1, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_pad_bottom(header, 4, 0);

  lv_obj_t *flight_header = lv_label_create(header);
  lv_label_set_text(flight_header, "FLIGHT");
  lv_obj_set_style_text_color(flight_header, kColorHeaderText, 0);
  lv_obj_set_flex_grow(flight_header, 1);

  lv_obj_t *speed_header = lv_label_create(header);
  lv_label_set_text(speed_header, "SPD");
  lv_obj_set_style_text_color(speed_header, kColorHeaderText, 0);
  lv_obj_set_width(speed_header, kSpeedColWidth);
  lv_obj_set_style_text_align(speed_header, LV_TEXT_ALIGN_RIGHT, 0);

  lv_obj_t *distance_header = lv_label_create(header);
  lv_label_set_text(distance_header, "DIST");
  lv_obj_set_style_text_color(distance_header, kColorHeaderText, 0);
  lv_obj_set_width(distance_header, kDistanceColWidth);
  lv_obj_set_style_text_align(distance_header, LV_TEXT_ALIGN_RIGHT, 0);

  lv_obj_t *alt_header = lv_label_create(header);
  lv_label_set_text(alt_header, "ALT");
  lv_obj_set_style_text_color(alt_header, kColorHeaderText, 0);
  lv_obj_set_width(alt_header, kAltitudeColWidth);
  lv_obj_set_style_text_align(alt_header, LV_TEXT_ALIGN_RIGHT, 0);

  lv_timer_create(motion_timer_cb, kMotionTickMs, this);
}

PlaneTableView::Row &PlaneTableView::ensure_row(size_t index) {
  while (rows_.size() <= index) {
    Row row;
    row.container = make_row_container(list_area_);

    // Recolor lets each label's per-aircraft color ride along in the same
    // lv_label_set_text_fmt() call (as a "#rrggbb text#" span) instead of a
    // separate lv_obj_set_style_text_color() call -- halves the LVGL calls
    // render() makes per row, per tick (see the FPS investigation).
    row.flight_label = lv_label_create(row.container);
    lv_label_set_long_mode(row.flight_label, LV_LABEL_LONG_CLIP);
    lv_label_set_recolor(row.flight_label, true);
    lv_obj_set_flex_grow(row.flight_label, 1);

    row.speed_label = lv_label_create(row.container);
    lv_label_set_recolor(row.speed_label, true);
    lv_obj_set_width(row.speed_label, kSpeedColWidth);
    lv_obj_set_style_text_align(row.speed_label, LV_TEXT_ALIGN_RIGHT, 0);

    row.distance_label = lv_label_create(row.container);
    lv_label_set_recolor(row.distance_label, true);
    lv_obj_set_width(row.distance_label, kDistanceColWidth);
    lv_obj_set_style_text_align(row.distance_label, LV_TEXT_ALIGN_RIGHT, 0);

    row.altitude_label = lv_label_create(row.container);
    lv_label_set_recolor(row.altitude_label, true);
    lv_obj_set_width(row.altitude_label, kAltitudeColWidth);
    lv_obj_set_style_text_align(row.altitude_label, LV_TEXT_ALIGN_RIGHT, 0);

    rows_.push_back(row);
  }
  return rows_[index];
}

void PlaneTableView::render() {
  const size_t shown = std::min(aircraft_.size(), kMaxTableRows);
  ensure_row(shown);

  // Re-derive nearest-first order every render (not just on update()) since
  // aircraft_ positions move between OpenSky refreshes.
  std::vector<size_t> order(aircraft_.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const Aircraft &aa = aircraft_[a];
    const Aircraft &ab = aircraft_[b];
    return aa.east_km * aa.east_km + aa.north_km * aa.north_km <
           ab.east_km * ab.east_km + ab.north_km * ab.north_km;
  });

  for (size_t i = 0; i < shown; ++i) {
    Row &row = rows_[i];
    const Aircraft &aircraft = aircraft_[order[i]];
    const float distance_km =
        std::sqrt(aircraft.east_km * aircraft.east_km + aircraft.north_km * aircraft.north_km);
    const lv_color_t color = plane_color::for_callsign(aircraft.callsign);
    char color_hex[7];
    std::snprintf(color_hex, sizeof(color_hex), "%02x%02x%02x", color.red, color.green, color.blue);

    const int speed_kts = static_cast<int>(std::lround(aircraft.speed_mps * kMpsToKnots));

    lv_label_set_text_fmt(row.flight_label, "#%s %s#", color_hex, aircraft.callsign.c_str());
    lv_label_set_text_fmt(row.speed_label, "#%s %d#", color_hex, speed_kts);
    lv_label_set_text_fmt(row.distance_label, "#%s %.1f#", color_hex, distance_km);
    if (aircraft.altitude_ft >= kAltitudeAbbreviateThresholdFt) {
      lv_label_set_text_fmt(row.altitude_label, "#%s %dk#", color_hex,
                            static_cast<int>(std::lround(aircraft.altitude_ft / 1000.0f)));
    } else {
      lv_label_set_text_fmt(row.altitude_label, "#%s %d#", color_hex, static_cast<int>(aircraft.altitude_ft));
    }
    lv_obj_clear_flag(row.container, LV_OBJ_FLAG_HIDDEN);
  }
  for (size_t i = shown; i < rows_.size(); ++i) {
    lv_obj_add_flag(rows_[i].container, LV_OBJ_FLAG_HIDDEN);
  }
}

void PlaneTableView::tick_motion() {
  constexpr float kDtS = static_cast<float>(kMotionTickMs) / 1000.0f;
  for (Aircraft &aircraft : aircraft_) {
    const float heading_rad = aircraft.heading_deg * kDegToRad;
    const float delta_km = aircraft.speed_mps * kDtS / 1000.0f;
    aircraft.east_km += delta_km * std::sin(heading_rad);
    aircraft.north_km += delta_km * std::cos(heading_rad);
  }
  render();
}

void PlaneTableView::motion_timer_cb(lv_timer_t *timer) {
  auto *self = static_cast<PlaneTableView *>(lv_timer_get_user_data(timer));
  self->tick_motion();
}

void PlaneTableView::update(std::span<const Contact> contacts) {
  aircraft_.clear();
  aircraft_.reserve(contacts.size());
  for (const Contact &contact : contacts) {
    const float bearing_rad = contact.bearing_deg * kDegToRad;
    Aircraft aircraft;
    aircraft.callsign = contact.callsign;
    aircraft.altitude_ft = contact.altitude_ft;
    aircraft.east_km = contact.distance_km * std::sin(bearing_rad);
    aircraft.north_km = contact.distance_km * std::cos(bearing_rad);
    aircraft.speed_mps = contact.ground_speed_mps;
    aircraft.heading_deg = contact.track_deg;
    aircraft_.push_back(aircraft);
  }
  render();
}
