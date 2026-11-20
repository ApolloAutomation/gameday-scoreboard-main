# Apollo

`apollo.png` is Apollo the dog from Apollo Automation's logo, exported by
Apollo Automation, who maintain this firmware, with the background clear
(the export's metadata removed, the pixels unchanged). The panel shows him
on its start-up and setup screens.

The logo and the Apollo Automation name are Apollo Automation's. The
repository's MIT license covers the code, not this artwork: use it only as
part of this firmware, and ask Apollo Automation before using it anywhere
else.

Don't edit `components/gameday/apollo_logo.h`: replace `apollo.png` and run
`python3 scripts/build_apollo.py`, which cuts the dog out, crops him with
his ledge, scales him to 64 rows and lays him on the screens' navy.
