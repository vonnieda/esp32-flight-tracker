#pragma once

#include <memory>

#include "cJSON.h"

// RAII handle for a cJSON_Parse() result: cJSON_Delete runs automatically on
// scope exit (including early returns), so callers can't forget it on one
// path while remembering it on another.
using CJsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

inline CJsonPtr cjson_parse(const char *json) {
  return CJsonPtr(cJSON_Parse(json), cJSON_Delete);
}
