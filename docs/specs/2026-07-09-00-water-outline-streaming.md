Continuation of docs/specs/2026-07-08-01-automatic-map-outlines.md Phase 2. Admin
boundaries (green) are working well on feature/automatic_maps, fetched from
Overpass and cached to flash. Water (blue) got ripped back out -- this is
about picking that up again.

What went wrong with water
---
Two crashes turned up testing on real hardware, both the same underlying
issue: Overpass's `around:radius` filter decides whether to *include* a way
in the response, but it does not clip the way's *geometry* to that radius.
An object with even one node inside the circle comes back with its entire
geometry, however long that is.

- Administrative boundary relations: `way(r); out geom;` pulled every member
  way of a matched relation -- for a country/state boundary that's the
  entire national/state border, not just the segment near home. Fixed by
  re-filtering the way selection with `(around:...)` too:
  `way(r)(around:radius,lat,lon);`. That works because relation membership
  and the around filter are two independent set operations we can chain.
- Water: `way["waterway"="river"](around:radius,lat,lon)` has no equivalent
  fix. A river is just a single way (not a relation), so there's no second
  selection step to re-filter -- if the Missouri River (my location) clips
  the query circle even once, the whole river's geometry comes back, however
  many hundred km long that is.

As a safety net (not a fix) I added a hard 128KB response-body cap in
http_fetch.cpp (main/http_fetch.cpp) -- past that, bytes are dropped so the
allocator can't be run out of memory, and the now-truncated/invalid JSON
just fails to parse, which every caller already treats as a normal fetch
failure + retry. That's a generically good hardening (protects every
http_fetch caller, not just the map), and should stay regardless of what we
do below. But it's why water reliably fails near me: not a bug, just the
cap doing its job against a genuinely oversized response.

Given that, water got dropped from main.cpp/map_client entirely rather than
shipping something that silently never works at my location.

Plan
---
1. Start with lakes only: query `natural=water` and drop `waterway=river`.
   Lakes are closed ways/areas, not long linear features threading across
   the map, so they shouldn't trigger the same unbounded-geometry problem in
   the common case. Get that rendering (blue, via the existing generic
   RadarView::add_map_outline) and confirmed stable before touching rivers
   again.
2. Then look at rivers again, but properly this time: I don't want response
   size to be a hard limitation the way the 128KB cap currently is. Explore
   streaming/incremental parsing of the Overpass response instead of
   buffering the whole body into a std::string and handing it to cJSON as a
   whole tree (main/map_client.cpp's fetch_and_clip does this today). cJSON
   is DOM-style, not streaming, so this likely means either:
   - a minimal incremental scanner for Overpass's specific known output
     shape (`elements: [ { type, geometry: [ {lat, lon}, ... ] }, ... ]`),
     clipping each point to the bbox as it's read instead of after the fact,
     so a too-long way just gets truncated to the useful portion instead of
     blowing the memory budget; or
   - feeding a SAX-style/event parser off the raw HTTP chunks as they arrive
     (http_fetch.cpp's append_body_handler already sees data incrementally
     via HTTP_EVENT_ON_DATA -- currently it just appends to a string).
   Either way the goal is the same: don't need to hold the full response (or
   a full parsed tree of it) in memory at once, so a long river doesn't need
   a size cap to be handled safely -- it just gets clipped to the relevant
   segment as we go.
