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

