Phase 1
---

Look at tools/make_map.py. This is used to generate the header file that
contains the map outlines. We want to generate this on the fly, without
having to hardcode anything, on the device. 

Look at the exisitng map_data.h for information about how it was generated
and then let's figure out how we can do the same on the ESP with much
less memory. Some of those files are quite large. We may need to either
handle them while streaming them, or perhaps look for different sources.

Ultimately, we want to be able to render a handful of lines that give the user
a picture of their area. For my current one it's just the state border between
KS and MO and it is very simple, but very informative.


Phase 2
---
We did the above work, and it works, but the 110m data is not very informative.
I tried the 50m data but we don't have the memory for that. Let's investigate
other options. The goal is to have a lightweight outline that gives the user
some context about the surrounding area. 

One option may be Overpass API. Here is an example query I found:

curl -X POST https://overpass-api.de/api/interpreter \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data-urlencode 'data=
[out:json][timeout:25];
(
  relation["boundary"="administrative"]["admin_level"="2"](around:15000,40.7128,-74.0060);
  relation["boundary"="administrative"]["admin_level"="4"](around:15000,40.7128,-74.0060);
);
way(r);
out geom;'

I want to see if we can render adminsitrative boundaries in green 
(like we currently do) and then add lake and/or river boundaries in blue.

Alternately, we could do a streaming parser for the 50m or even the 10m so we
don't have to load the whole file into memory, but I've been told Overpass is
a better option for what we're trying to do.
