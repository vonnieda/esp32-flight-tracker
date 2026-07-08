#include "aircraft_type_client.hpp"

#include <cstdio>
#include <cstring>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "http_fetch.hpp"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "aircraft_type";
constexpr char kNvsPartition[] = "actypes";
constexpr char kNvsNamespace[] = "cache";

// Bounds how many icao24s can be queued for lookup at once; with the queue
// full, annotate() just skips enqueuing until the background task catches
// up, and retries on the next poll.
constexpr UBaseType_t kPendingQueueCapacity = 24;

// Comfortably under the "actypes" partition's real capacity (each entry is
// a single-byte NVS value, ~1500+ fit in its 64KB); a wholesale reset if
// this is ever hit is fine since entries repopulate at a few per poll.
constexpr size_t kMaxCacheEntries = 1024;

// icao24 is 6 hex chars; queue items are fixed-size so FreeRTOS can copy
// them by value.
struct PendingLookup {
  char icao24[8];
};

// Rotorcraft ICAO designators, common US civil/med-evac/police/military
// types. Checked before the airliner prefixes since a few (B407, B412, ...)
// would otherwise be caught by fixed-wing prefix rules.
constexpr const char *kHelicopters[] = {
    "R22",  "R44",  "R66",  "B06",  "B105", "B212", "B407", "B412", "B429",
    "B430", "B505", "B47G", "EC20", "EC25", "EC30", "EC35", "EC45", "EC55",
    "EC75", "AS50", "AS55", "AS65", "H500", "MD52", "MD60", "S61",  "S76",
    "S92",  "A109", "A119", "A139", "A169", "A189", "H47",  "H53",  "H60",
    "H64",  "UH1",
};

// Airliner/large-transport designators that a prefix rule doesn't cover:
// A320neo family, A220, 737 MAX, E-Jet variants, and common freighters.
constexpr const char *kAirlinersExact[] = {
    "A19N", "A20N", "A21N", "BCS1", "BCS3", "B37M", "B38M", "B39M", "B3XM",
    "E135", "E145", "E45X", "E75L", "E75S", "MD90", "SF34", "C130", "C30J",
    "K35R",
};

// Whole families where every designator sharing the prefix is a large
// transport: Airbus A3xx, Boeing 7x7, MD-80/-11, E-Jets, CRJs, Dash 8, ATR.
constexpr const char *kAirlinerPrefixes[] = {
    "A3", "B7", "MD1", "MD8", "E17", "E19", "E29", "CRJ", "DH8", "AT4", "AT7",
};
}  // namespace

PlaneClass classify_type_designator(const std::string &designator) {
  if (designator.empty()) {
    return PlaneClass::kUnknown;
  }
  for (const char *heli : kHelicopters) {
    if (designator == heli) {
      return PlaneClass::kHelicopter;
    }
  }
  for (const char *airliner : kAirlinersExact) {
    if (designator == airliner) {
      return PlaneClass::kAirliner;
    }
  }
  for (const char *prefix : kAirlinerPrefixes) {
    if (designator.rfind(prefix, 0) == 0) {
      return PlaneClass::kAirliner;
    }
  }
  // Any other known fixed-wing type: GA pistons, turboprops, bizjets.
  return PlaneClass::kSmall;
}

void AircraftTypeClient::start() {
  mutex_ = xSemaphoreCreateMutex();
  pending_queue_ = xQueueCreate(kPendingQueueCapacity, sizeof(PendingLookup));

  esp_err_t err = nvs_flash_init_partition(kNvsPartition);
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase_partition(kNvsPartition);
    err = nvs_flash_init_partition(kNvsPartition);
  }
  if (err == ESP_OK) {
    err = nvs_open_from_partition(kNvsPartition, kNvsNamespace, NVS_READWRITE, &nvs_handle_);
  }
  if (err == ESP_OK) {
    load_cache_from_nvs();
  } else {
    ESP_LOGW(kTag, "type cache unavailable, falling back to RAM-only (%s)", esp_err_to_name(err));
  }

  xTaskCreate(lookup_task_trampoline, "aircraft_type", 8192, this, tskIDLE_PRIORITY + 1, nullptr);
}

void AircraftTypeClient::load_cache_from_nvs() {
  nvs_iterator_t it = nullptr;
  esp_err_t err = nvs_entry_find_in_handle(nvs_handle_, NVS_TYPE_U8, &it);
  while (err == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    uint8_t raw = 0;
    if (nvs_get_u8(nvs_handle_, info.key, &raw) == ESP_OK) {
      cache_[info.key] = static_cast<PlaneClass>(raw);
    }
    err = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  ESP_LOGI(kTag, "loaded %zu cached airframe types from flash", cache_.size());
}

void AircraftTypeClient::annotate(std::vector<Contact> &contacts) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (Contact &contact : contacts) {
    if (contact.icao24.empty()) {
      continue;
    }

    const auto cached = cache_.find(contact.icao24);
    if (cached != cache_.end()) {
      contact.plane_class = cached->second;
      continue;
    }
    if (in_flight_.count(contact.icao24) != 0) {
      continue;  // Already queued (or being looked up); don't pile on.
    }

    PendingLookup lookup{};
    std::strncpy(lookup.icao24, contact.icao24.c_str(), sizeof(lookup.icao24) - 1);
    if (xQueueSend(pending_queue_, &lookup, 0) == pdTRUE) {
      in_flight_.insert(contact.icao24);
    }
    // Queue full: skip for now, retry next poll.
  }
  xSemaphoreGive(mutex_);
}

void AircraftTypeClient::lookup_task_trampoline(void *arg) {
  static_cast<AircraftTypeClient *>(arg)->lookup_task();
}

void AircraftTypeClient::store(const std::string &icao24, PlaneClass plane_class) {
  if (cache_.size() >= kMaxCacheEntries) {
    cache_.clear();
    if (nvs_handle_ != 0) {
      nvs_erase_all(nvs_handle_);
    }
  }
  cache_[icao24] = plane_class;
  if (nvs_handle_ != 0) {
    nvs_set_u8(nvs_handle_, icao24.c_str(), static_cast<uint8_t>(plane_class));
    nvs_commit(nvs_handle_);
  }
}

void AircraftTypeClient::lookup_task() {
  while (true) {
    PendingLookup lookup;
    xQueueReceive(pending_queue_, &lookup, portMAX_DELAY);
    const std::string icao24(lookup.icao24);

    char url[80];
    std::snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/aircraft/%s", icao24.c_str());

    std::string body;
    int status = 0;
    if (http_fetch(url, nullptr, nullptr, nullptr, body, &status) != ESP_OK) {
      // Transport error; drop from in_flight_ so a later poll can retry.
      xSemaphoreTake(mutex_, portMAX_DELAY);
      in_flight_.erase(icao24);
      xSemaphoreGive(mutex_);
      continue;
    }

    PlaneClass plane_class = PlaneClass::kUnknown;
    bool resolved = true;
    if (status == 200) {
      cJSON *root = cJSON_Parse(body.c_str());
      if (root != nullptr) {
        const cJSON *response = cJSON_GetObjectItemCaseSensitive(root, "response");
        const cJSON *aircraft = cJSON_GetObjectItemCaseSensitive(response, "aircraft");
        const cJSON *icao_type = cJSON_GetObjectItemCaseSensitive(aircraft, "icao_type");
        if (cJSON_IsString(icao_type)) {
          plane_class = classify_type_designator(icao_type->valuestring);
          ESP_LOGI(kTag, "%s: %s -> class %d", icao24.c_str(), icao_type->valuestring,
                   static_cast<int>(plane_class));
        }
        cJSON_Delete(root);
      }
    } else if (status != 404) {
      // 404 means adsbdb doesn't know the airframe (common for military and
      // blocked registrations) -- cache that as kUnknown so we don't re-ask.
      // Other statuses (rate limiting, server errors) are worth retrying.
      resolved = false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    in_flight_.erase(icao24);
    if (resolved) {
      store(icao24, plane_class);
    }
    xSemaphoreGive(mutex_);
  }
}
