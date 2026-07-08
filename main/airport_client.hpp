#pragma once

#include <string>
#include <vector>

#include "esp_err.h"

// An airport to label on the scope, in the same flat-earth coordinates as
// Contact (kilometers east/north of the receiver).
struct Airport {
  std::string ident;  // Preferably the IATA code (e.g. "MCI"), else ICAO.
  float east_km = 0;
  float north_km = 0;
};

namespace airport_client {

// Fetches airports within range_km of home from the aviationweather.gov
// airport API (public, keyless, FAA data -- so US coverage only) and
// converts them to receiver-relative coordinates. Filters out heliports and
// private strips so only "real" airports (the kind with a tower and an IATA
// code) get labeled -- showing every helipad and grass strip cluttered the
// scope. One call at startup is enough; the result is static.
esp_err_t fetch_airports(float home_lat_deg, float home_lon_deg, float range_km,
                         std::vector<Airport> &out_airports);

}  // namespace airport_client
