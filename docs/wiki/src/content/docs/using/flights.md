---
title: "Flight Tracker"
description: "The planes flying near a spot you pick, on the main board or in the second area: the nearest plane's flight, airline, route, type, height, speed, distance and direction, and a list of the nearest."
sidebar:
  order: 10.8
---

The Flight Tracker is a display the panel can show in place of the scoreboard: the planes in the sky near a spot you pick. Pick it for the main board under **Showing** in the Panel card on the device page, or for the [second area](/using/panel-sizes/#the-second-area) under **Shows**. There's nothing to sign up for: the flight data comes from free services that need no account or key.

Home Assistant's entities keep following the scoreboard's game while the Flight Tracker is on the panel.

## Set it up

The device page has a **Flight Tracker** card. It needs a home location before it shows any planes.

1. Under **Home location**, type a town, a ZIP or postal code, or a latitude and longitude like **40.64, -73.78**, and tap **Find**.
2. For a town or a code, your browser looks it up and lists what it found, like **Brooklyn, New York, United States** and **Brooklyn, Iowa, United States**. Tap the right one. When there's only one, it's picked for you. A latitude and longitude goes straight to the panel.
3. Pick how far from home a plane can be under **Within**: 5 to 100 miles, or 5 to 150 km outside the US and UK. A new panel starts at 10 miles.
4. Choose **Flight Tracker** under **Showing** in the Panel card, or under **Shows** for the second area.

The home is [Weather](/using/weather/)'s too: setting or clearing it in the Flight Tracker card or the Weather card changes both.

**Planes near** shows the spot the panel has, with **Clear** to remove it. The line at the bottom of the card says what the panel is doing, like **12 planes nearby, from Flightradar24.**

Under that line, the card shows a photo of the plane on the panel's card, from Planespotters.net, with its callsign and the photographer's name. Tap the photo or the name to open the photo's page on Planespotters.net. Your browser looks the photo up itself, by the plane's address and then its registration, and keeps what it found while the page is open. A plane with no photo there shows none.

The panel uses your time zone's units: in the US and UK time zones feet, mph and miles, and elsewhere meters, km/h and km.

**Your own receiver** is optional: an ADS-B receiver on your home network. For tar1090 its address is enough, like **192.168.1.50**; for PiAware, give its whole aircraft.json address. See [Your own receiver](#your-own-receiver).

## What the panel shows

The nearest plane's card has:

- its callsign, like **DAL2503**, or its registration when it has no callsign, in red during an emergency (squawk 7500, 7600 or 7700, or an emergency the plane declares; an air ambulance's lifeguard flag or low fuel isn't one)
- the airline, like **Delta**, or the aircraft type for a private plane
- the route, like **ORD-LGA**, in gold, when it's known; otherwise the flight number, like **DL2503**
- the aircraft type, like **Airbus A220-300** (just the code, like **BCS3**, on one 64x64 panel)
- the altitude and the ground speed
- how far away it is and in which direction, like **2.4 mi SW**
- except on a 64x64 area, whether it's climbing, descending or level

The card also shows the airline's logo and a picture of the plane.

- **The logo** is the airline painted on the plane, like Delta's red triangle. A flight that Republic flies for American shows American's logo, with **Republic** as the airline's name, when Flightradar24 says so. Until the logo comes in, or for an airline whose logo is only a long name or can't be found, the airline's code shows in its place, like **WN** for Southwest or **EJA** for NetJets.
- **The picture** is the plane's shape from the side, bigger for bigger planes: a four-engine jumbo, a twin-aisle jet, a single-aisle jet, a regional jet, a turboprop airliner, a business jet, a small twin or single propeller plane, a helicopter, a fighter, a military transport, or a plain plane when the type isn't known. It faces the way the plane is flying, and its tail is in the airline's color. A glider gets the small propeller plane. A balloon, an airship, a drone or skydivers get no picture.
- **On a 64x64 area** the logo and a smaller callsign share the top, with the route or flight number under the callsign, then the picture, then the type code and the speed, the altitude and where it is. The airline's name is left to the ticker. A plane with no airline the panel knows, like a private plane, has no logo: its callsign is small with the route or flight number under it, or big when it has neither, and the picture is under that.
- **On 128 wide and more**, the logo is at the top right, after the airline's name, and the picture is right of the route and the type. A type name too long for its space leaves off its maker, like **737 MAX 8**.
- **A photo of the plane** takes the picture's place on a card 128 wide or more (one 128x64 panel, either half of two rows of two, and three or four panels in a row), once it comes in: a real photo of that very plane from [Planespotters.net](https://www.planespotters.net), found by the plane's address or registration. The panel shows the photo's middle, cut to the card's shape, with the sky and the ground behind the plane dimmed so the plane stands out. Once the photo is in, the ticker credits the photographer right after that plane, like **Photo: Jane Doe**, and keeps the credit while a 128x64 panel shows the list, so the ticker's words don't change as the screens take turns. Until the photo comes in, and for a plane Planespotters has no photo of, the picture shows. A 64x64 area keeps the picture.

The list of the nearest planes, nearest first, has each one's callsign, type code, altitude and distance, or just the callsign and the distance where the list is one panel wide. Its first line says how many planes are nearby.

:::note[Coming in the next release]
**The picture moves.** When the card shows the plane's picture rather than a photo, the picture moves in a small sky of its own: a helicopter's rotor turns and it bobs gently where there's room, a propeller plane's propellers spin, and a jet leaves a short dashed contrail as clouds drift past it. A plane whose type isn't known keeps a still picture. Behind it, clouds drift slowly by, a few birds glide across now and then by day, the sun sits in a corner by day, and the moon (in its phase) and a few softly twinkling stars come out at night. Wind streaks show on a gusty day. When [Weather](/using/weather/) is set up and has read the forecast for the same home, the sky follows it: clear, partly cloudy or overcast, with rain, snow or fog, and day and night from that day's sunrise and sunset. Without Weather, day and night come from the sun's place over your home, and the clouds are made up. Each plane gets its own sky, and it changes every ten minutes.

Everything moves slowly and stays dim, so the plane is always the brightest thing in the picture and nothing flashes. During the night hours, and while **Off or dim** has dimmed the panel, the picture and its sky hold still.

The notes the Flight Tracker shows instead of a plane, like **Looking for planes**, have a band of the same sky above their words when the area is at least 64 LEDs tall.
:::

| Area | What it shows |
| --- | --- |
| 64x64 (one panel, or the right panel of three wide) | The card and the list take turns, about 8 seconds each. A speed too wide for the card, like 1185 km/h, shows as the number alone. |
| 128x64 (one 128x64 panel, the right two panels of four wide, either half of two rows of two) | The card and the list take turns, about 8 seconds each. |
| Three or four panels in a row, with no second area | The card on the left and the list beside it, together. |

**Full screen on two rows of two.** With **Full screen** on under **Shows** (see [The second area](/using/panel-sizes/#the-second-area)), the Flight Tracker fills all four panels, 128x128, and the card and the list still take turns, about 8 seconds each:

- **The card**: the airline's logo big, 32 LEDs square, at the top left, the callsign in big letters beside it with the airline's name and the plane's type under it, then the route in big letters, like **ATL-BDL**, with the flight number at its right. The plane's photo fills the width of the panel under that, about 126 by 52 LEDs, cut to that shape, and until it comes in, or with no photo, the plane's picture shows there at twice its size, or three times for a small plane like a business jet or a helicopter. Under the picture are the altitude and the speed, then how far away and in which direction, and whether it's climbing, level or descending.
- **The list**: the nearest seven planes, each with its airline's logo and two lines: the callsign and the distance, then the type, the route and the altitude.

:::note[Coming in the next release]
**A radar on the extra panels.** When the Flight Tracker has the whole of four wide (with **Nothing** picked, or **Full screen** on) or of two rows of two (with **Full screen** on), half the panels show a radar of the planes around your home:

- On four wide, the nearest plane's card stays on the left two panels, as it is today, and the right two panels show the radar and the nearby list in turn, about 8 seconds each.
- On two rows of two, the top half shows the 128x64 card, with its photo or picture, and the bottom half shows the radar and the nearby list in turn. This takes the place of the full-screen tall card and tall list: the list shows six planes with their type, altitude and distance, and the routes of the nearest three stay in the ticker.

On the radar, your home is the white dot in the middle, north is up (the **N** on the ring), and the ring is your **Within** distance, labeled at the bottom right, like **10MI**. When the distance is an even number, a dotted ring marks half of it. Each plane is a small arrow pointing the way it's flying, or a dot when its heading isn't known; a short dash is a plane on the ground. The plane on the card is yellow with brackets around it, so the two halves read together, and a plane in an emergency is red. Planes farther than your distance are left out.

The nearest plane has its callsign beside it (its registration when it has no callsign). The others carry the number of their row in the nearby list, which is numbered **1** to **6** on these two layouts, so a row in the list can be found on the radar. A number with no room beside its plane is left off.

Under the planes, the radar shows the same [map landmarks](/using/weather/#what-the-panel-shows) as Weather's radar, when your browser has added them: nearby towns, state lines, coastlines and highways, dim so the planes stand out. A home by the 180th meridian sees planes on both sides of it, but its landmarks stop at the meridian. Without them the radar shows the rings and the planes on black. Map landmarks from [Natural Earth](https://www.naturalearthdata.com) and [GeoNames](https://www.geonames.org) ([CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)).

Every other size, and the Flight Tracker in a second area, shows as before.
:::

The ticker along the bottom says how many planes are nearby, then the nearest three, with the airline, the type, the route, the altitude, the speed and where each one is.

The card stays on the same plane until another one is clearly nearer, so it doesn't flip between two planes at about the same distance. When nothing nearby is in the air, planes on the ground show instead, when the flight data lists them.

The panel shows a message instead of planes when it has none to show:

- **Set your location on the device page**: there's no home location yet.
- **Looking for planes**: the first answer hasn't come in yet, or the Flight Tracker has just come back on screen.
- **No planes nearby**, with the distance under it: nothing is flying within your **Within** distance right now. A plane the flight data has no recent position for doesn't count as nearby. (In the next release the panel looks farther first, as below.)
- **Can't reach flight data**: three reads in a row failed, and the panel has no planes from the last 3 minutes. The panel keeps trying.

:::note[Coming in the next release]
**The Flight Tracker always has something to show.** When a read finds no plane within your **Within** distance, the next read looks all the way out to 250 miles (400 km, or 200 nautical miles), at the usual pace. Meanwhile it shows **Looking farther**, with the distance it just found empty under it, like **Nothing within 10 mi**. From what that read finds, the panel picks the smallest of the distances 15, 25, 50, 100, 150 and 250 miles (25, 50, 100, 150, 250 and 400 km, or up to 200 nautical miles) that holds the nearest plane, shows the planes within it, and keeps reading one distance wider so a plane drifting out stays found.

Once it finds a plane, it shows the nearest ones it found and says plainly that they're farther than your distance:

- every distance is in yellow instead of green;
- the list's first line says how far it looked, like **Within 50 mi**, in place of **Nearby**;
- the ticker starts with **No planes within 10 mi. Nearest: 42 mi SW**;
- on the radar the ring is the wider distance, its label in yellow, like **50MI**, and a dotted ring with a small label, like **10**, marks your own distance (left out when it would be a tiny circle).

Your **Within** distance never changes. As soon as a plane is inside it again, the panel shows only the planes inside it, as usual; it keeps reading the wider circle for 5 minutes after the last plane it found farther out, so a plane passing through doesn't make it flip back and forth, then reads your distance again. A new home, distance or receiver starts over at your distance.

When there's nothing at all out to 250 miles (400 km, 200 nautical miles), the panel shows a calm sky with **No planes nearby right now** and **Checked out to 250 mi**, and reads every 2 minutes until a plane shows up. The device page's line says so too, like **No planes within 10 mi; showing the nearest, up to 50 mi away.**

A wider read asks the flight data service for a bigger circle, so it's a larger download, and it only happens while the sky near you is empty.
:::

At night, with [night hours](/using/brightness-and-clock/#night-hours) on, the clock takes the Flight Tracker's place until they end, and the device page says so.

With the **Idle display** on **Off or dim** and the Flight Tracker on the main board, the panel stays dark or dim while it has no planes to show within your **Within** distance, and lights up when one comes near. It keeps reading while it's dark, so a plane, not a game, lights it.

## Where the data comes from

- **Flightradar24**: the panel reads Flightradar24's public live map feed about every 20 seconds while the Flight Tracker is on screen. It has each plane's route and flight number too. One read for the planes near a city is about 5 KB, about 1 MB an hour.
- **adsb.fi, then adsb.lol**: when Flightradar24 fails twice in a row, or turns the panel away, the panel switches to adsb.fi on its own, and to adsb.lol if adsb.fi is out too. adsb.lol is read at most every 3 minutes. The panel goes back to Flightradar24 once its wait is over, longer each time it fails again, and as long as it asks when it turns the panel away. A read from adsb.fi is larger, about 30 KB for 50 planes, about 5 MB an hour.
- **Routes**: for an airline flight whose source has no full route (adsb.fi, adsb.lol, your own receiver, and some Flightradar24 flights), the panel looks the route up from adsb.im, at most every 30 seconds, over plain HTTP, or from adsbdb while adsb.im is failing or turning the panel away. It shows a route only when it fits where the plane is and the way it's heading, and keeps each route for 3 hours.
- **Nothing while it's off screen**: the Flight Tracker reads only while it's on the main board or in the second area. Off screen, the device page says it isn't reading planes, rather than an old count.
- **Near the 180th meridian**: Flightradar24's box can't reach across it, so for a home whose circle crosses it, like in Fiji, the panel reads adsb.fi first. The card's line then names adsb.fi without saying Flightradar24 is out.

The card's line says which source the planes on the panel came from.

**Airline logos** come from Google Flights' logo images (www.gstatic.com) or FlightAware's (www.flightaware.com), whichever reads better at 16 pixels, with the other as a backup. The panel downloads an airline's logo, a few KB, the first time one of its planes is on the card, and keeps it until it restarts or needs the room for newer logos. A logo that's missing, or can't be read at 16 pixels, isn't asked for again for 6 hours; the airline's code shows meanwhile.

**Plane photos** come from Planespotters.net. While a card 128 wide or more is on screen, the panel asks Planespotters' photo service (api.planespotters.net) about the plane on it, once a plane and at most every 5 seconds, then downloads the photo it names, about 20 to 50 KB, from t.plnspttrs.net. It remembers a plane's photo for a day and that a plane has none for 12 hours, and keeps at most eight photos and [Weather](/using/weather/)'s radar frames together, so they don't crowd out team logos. When Planespotters fails or turns the panel away, the panel waits longer each time before asking again, and as long as Planespotters asks.

## Your own receiver

With an ADS-B receiver on your network, the panel reads it first, every 10 seconds. When it fails twice in a row, the panel reads Flightradar24 and the backups above until the receiver answers again. A changed address is read right away.

The receiver's file lists every plane it hears, some with no recent position; those aren't counted as nearby.

- A bare address, like **192.168.1.50** or **piaware.local**, reads tar1090's planes at `/tar1090/data/aircraft.json`.
- A whole address ending in `.json` is read as it is, like `http://192.168.1.50/skyaware/data/aircraft.json` for PiAware's SkyAware or `http://192.168.1.50/dump1090-fa/data/aircraft.json`.
- readsb's own API, an address with port 30152, `/re-api` or a `?` in it, gets the circle around your home added.

The receiver's planes need no internet, but their routes still come from adsb.im or adsbdb.

## Your location

The home location stays on the panel, to four decimal places (about 10 meters), with the place's name if you looked one up. Each read sends the flight data service a box around it, not the spot itself: the center is rounded to about a kilometer first, and the box reaches a little past your **Within** distance all around it. adsb.fi and adsb.lol get that rounded center and the distance. The panel still works out each plane's distance and direction from your exact spot. Route lookups send only the planes' callsigns and positions, over plain HTTP to adsb.im. A logo download sends only the airline's code, never your location, and a photo lookup only the plane's address or registration.

Looking up a town or a ZIP code goes from your browser to Open-Meteo's place search, not from the panel.

:::note[Coming in the next release]
Setting the home on the device page also has your browser read map landmarks for [Weather's radar](/using/weather/#what-the-panel-shows), and for the flight radar on four panels in a row or two rows of two, from this project's site: the files for the squares of about 400 km around the home, never the home itself. The card's **Radar map** row shows how that went, with **Add landmarks** for a home set before; see [The device page](/using/device-page/). Map landmarks from [Natural Earth](https://www.naturalearthdata.com) and [GeoNames](https://www.geonames.org) ([CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)).
:::

**Clear** removes the location from the panel, and a [factory reset](/using/buttons-and-resets/) does too.
