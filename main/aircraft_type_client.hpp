#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "contact.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "nvs.h"

// Resolves each airframe's PlaneClass from its ICAO type designator via the
// adsbdb.com public API (https://api.adsbdb.com/v0/aircraft/<icao24>).
//
// Resolved results are cached both in RAM and in a dedicated NVS partition
// ("actypes", see partitions.csv) keyed by icao24, so a reboot doesn't have
// to re-run every lookup from scratch -- near a busy airport that was
// leaving icons generic for minutes after every restart.
//
// The actual network lookups run on a dedicated background task so they
// never sit on the OpenSky poll task's critical path -- annotate() only
// ever does fast in-RAM cache lookups, so a burst of newly-seen aircraft
// (each costing a ~1s+ TLS handshake to resolve) can't delay the
// position/altitude update the rest of the UI is waiting on. Contacts show
// the generic icon (kUnknown) until a background lookup resolves them,
// which then shows up automatically on the next poll.
class AircraftTypeClient {
 public:
  // Opens the on-flash cache and spawns the background lookup task. Call
  // once, before the first annotate().
  void start();

  // Fills each contact's plane_class from whatever's cached and enqueues
  // any new icao24 for the background task to resolve. Never blocks on the
  // network -- safe to call from the poll task on every fetch.
  void annotate(std::vector<Contact> &contacts);

 private:
  static void lookup_task_trampoline(void *arg);
  void lookup_task();
  void load_cache_from_nvs();
  // Records a resolved class in the in-RAM cache and, if the NVS partition
  // opened successfully, on flash too. Caller must hold mutex_.
  void store(const std::string &icao24, PlaneClass plane_class);

  SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t pending_queue_ = nullptr;
  nvs_handle_t nvs_handle_ = 0;  // 0 if the cache partition failed to open.
  std::map<std::string, PlaneClass> cache_;
  // icao24s already queued (or being looked up), so a slow lookup doesn't
  // get re-enqueued every poll while it's still in flight.
  std::set<std::string> in_flight_;
};

// Maps an ICAO type designator (e.g. "B739", "C172", "EC35") to a broad
// class. Exposed for testing; the lookup task applies it to lookup results.
PlaneClass classify_type_designator(const std::string &designator);
