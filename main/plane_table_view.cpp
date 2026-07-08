#include "plane_table_view.hpp"

#include <algorithm>
#include <cmath>

#include "plane_color.hpp"

namespace {
constexpr lv_color_t kColorBackground = LV_COLOR_MAKE(0x00, 0x00, 0x00);
constexpr lv_color_t kColorHeaderText = LV_COLOR_MAKE(0x00, 0x8f, 0x11);
constexpr lv_color_t kColorHeaderRule = LV_COLOR_MAKE(0x00, 0x4d, 0x14);
constexpr int kSpeedColWidth = 36;
constexpr int kDistanceColWidth = 38;
constexpr int kAltitudeColWidth = 40;
constexpr float kMpsToKnots = 1.943844f;

// Above this, altitude is abbreviated to the nearest thousand feet with a
// "k" suffix (e.g. "27k") rather than the full value, to keep the column
// narrow.
constexpr float kAltitudeAbbreviateThresholdFt = 10000.0f;

// Caps the list to a screenful regardless of how many contacts the (wider)
// query radius returns; nearest-first sort means anything beyond this is
// the least relevant traffic anyway.
constexpr size_t kMaxTableRows = 11;

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

float distance_squared_km(const Contact &contact) {
  return contact.east_km * contact.east_km + contact.north_km * contact.north_km;
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
}

void PlaneTableView::ensure_row_count(size_t count) {
  while (rows_.size() < count) {
    Row row;
    row.container = make_row_container(list_area_);

    row.flight_label = lv_label_create(row.container);
    lv_label_set_long_mode(row.flight_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_flex_grow(row.flight_label, 1);

    row.speed_label = lv_label_create(row.container);
    lv_obj_set_width(row.speed_label, kSpeedColWidth);
    lv_obj_set_style_text_align(row.speed_label, LV_TEXT_ALIGN_RIGHT, 0);

    row.distance_label = lv_label_create(row.container);
    lv_obj_set_width(row.distance_label, kDistanceColWidth);
    lv_obj_set_style_text_align(row.distance_label, LV_TEXT_ALIGN_RIGHT, 0);

    row.altitude_label = lv_label_create(row.container);
    lv_obj_set_width(row.altitude_label, kAltitudeColWidth);
    lv_obj_set_style_text_align(row.altitude_label, LV_TEXT_ALIGN_RIGHT, 0);

    rows_.push_back(row);
  }
}

void PlaneTableView::update(std::span<const Contact> contacts) {
  std::vector<const Contact *> nearest;
  nearest.reserve(contacts.size());
  for (const Contact &contact : contacts) {
    nearest.push_back(&contact);
  }
  std::sort(nearest.begin(), nearest.end(), [](const Contact *a, const Contact *b) {
    return distance_squared_km(*a) < distance_squared_km(*b);
  });

  const size_t shown = std::min(nearest.size(), kMaxTableRows);
  ensure_row_count(shown);

  for (size_t i = 0; i < shown; ++i) {
    Row &row = rows_[i];
    const Contact &contact = *nearest[i];
    const float distance_km = std::sqrt(distance_squared_km(contact));
    const int speed_kts = static_cast<int>(std::lround(contact.ground_speed_mps * kMpsToKnots));

    const lv_color_t color = plane_color::for_altitude_ft(contact.altitude_ft);
    for (lv_obj_t *label :
         {row.flight_label, row.speed_label, row.distance_label, row.altitude_label}) {
      lv_obj_set_style_text_color(label, color, 0);
    }

    lv_label_set_text(row.flight_label, contact.callsign.c_str());
    lv_label_set_text_fmt(row.speed_label, "%d", speed_kts);
    lv_label_set_text_fmt(row.distance_label, "%.1f", distance_km);
    if (std::isnan(contact.altitude_ft)) {
      lv_label_set_text(row.altitude_label, "-");
    } else if (contact.altitude_ft >= kAltitudeAbbreviateThresholdFt) {
      lv_label_set_text_fmt(row.altitude_label, "%dk",
                            static_cast<int>(std::lround(contact.altitude_ft / 1000.0f)));
    } else {
      lv_label_set_text_fmt(row.altitude_label, "%d", static_cast<int>(contact.altitude_ft));
    }
    lv_obj_clear_flag(row.container, LV_OBJ_FLAG_HIDDEN);
  }
  for (size_t i = shown; i < rows_.size(); ++i) {
    lv_obj_add_flag(rows_[i].container, LV_OBJ_FLAG_HIDDEN);
  }
}
