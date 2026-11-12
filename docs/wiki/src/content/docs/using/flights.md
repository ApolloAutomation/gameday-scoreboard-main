---
title: "Flight Tracker"
description: "The planes flying near a spot you pick, on the main board or in the second area: the nearest plane's flight, airline, route, type, height, speed, distance and direction, and a list of the nearest."
sidebar:
  order: 10.8
---

:::note[Coming in the next release]
The Flight Tracker is a display the panel can show in place of the scoreboard: the planes in the sky near a spot you pick. Pick it for the main board under **Showing** in the Panel card on the device page, or for the [second area](/using/panel-sizes/#the-second-area) under **Shows**. There's nothing to sign up for: the flight data comes from free services that need no account or key.

Home Assistant's entities keep following the scoreboard's game while the Flight Tracker is on the panel.
:::

## Set it up

:::note[Coming in the next release]
The device page has a **Flight Tracker** card. It needs a home location before it shows any planes.

1. Under **Home location**, type a town, a ZIP or postal code, or a latitude and longitude like **40.64, -73.78**, and tap **Find**.
2. For a town or a code, your browser looks it up and lists what it found, like **Brooklyn, New York, United States** and **Brooklyn, Iowa, United States**. Tap the right one. When there's only one, it's picked for you. A latitude and longitude goes straight to the panel.
3. Pick how far from home a plane can be under **Within**: 5 to 100 miles, or 5 to 150 km outside the US and UK. A new panel starts at 10 miles.
4. Choose **Flight Tracker** under **Showing** in the Panel card, or under **Shows** for the second area.

**Planes near** shows the spot the panel has, with **Clear** to remove it. The line at the bottom of the card says what the panel is doing, like **12 planes nearby, from Flightradar24.**

The panel uses your time zone's units: in the US and UK time zones feet, mph and miles, and elsewhere meters, km/h and km.

**Your own receiver** is optional: an ADS-B receiver on your home network. For tar1090 its address is enough, like **192.168.1.50**; for PiAware, give its whole aircraft.json address. See [Your own receiver](#your-own-receiver).
:::

## What the panel shows

:::note[Coming in the next release]
The nearest plane's card has:

- its callsign, like **DAL2503**, or its registration when it has no callsign, in red during an emergency (squawk 7500, 7600 or 7700, or an emergency the plane declares; an air ambulance's lifeguard flag or low fuel isn't one)
- the airline, like **Delta**, or the aircraft type for a private plane
- the route, like **ORD-LGA**, in gold, when it's known; otherwise the flight number, like **DL2503**
- the aircraft type, like **Airbus A220-300** (just the code, like **BCS3**, on one 64x64 panel)
- the altitude and the ground speed
- how far away it is and in which direction, like **2.4 mi SW**
- except on a 64x64 area, whether it's climbing, descending or level

The list of the nearest planes, nearest first, has each one's callsign, type code, altitude and distance, or just the callsign and the distance where the list is one panel wide. Its first line says how many planes are nearby.

| Area | What it shows |
| --- | --- |
| 64x64 (one panel, or the right panel of three wide) | The card and the list take turns, about 8 seconds each. A speed too wide for the card, like 1185 km/h, shows as the number alone. |
| 128x64 (one 128x64 panel, the right two panels of four wide, either half of two rows of two) | The card and the list take turns, about 8 seconds each. |
| Three or four panels in a row, with no second area | The card on the left and the list beside it, together. |

The ticker along the bottom says how many planes are nearby, then the nearest three, with the airline, the type, the route, the altitude, the speed and where each one is.

The card stays on the same plane until another one is clearly nearer, so it doesn't flip between two planes at about the same distance. When nothing nearby is in the air, planes on the ground show instead, when the flight data lists them.

The panel shows a message instead of planes when it has none to show:

- **Set your location on the device page**: there's no home location yet.
- **Looking for planes**: the first answer hasn't come in yet, or the Flight Tracker has just come back on screen.
- **No planes nearby**, with the distance under it: nothing is flying within your **Within** distance right now. A plane the flight data has no recent position for doesn't count as nearby.
- **Can't reach flight data**: three reads in a row failed, and the panel has no planes from the last 3 minutes. The panel keeps trying.

At night, with [night hours](/using/brightness-and-clock/#night-hours) on, the clock takes the Flight Tracker's place until they end, and the device page says so.

With the **Idle display** on **Off or dim** and the Flight Tracker on the main board, the panel stays dark or dim while it has no planes to show within your **Within** distance, and lights up when one comes near. It keeps reading while it's dark, so a plane, not a game, lights it.
:::

## Where the data comes from

:::note[Coming in the next release]
- **Flightradar24**: the panel reads Flightradar24's public live map feed about every 20 seconds while the Flight Tracker is on screen. It has each plane's route and flight number too. One read for the planes near a city is about 5 KB, about 1 MB an hour.
- **adsb.fi, then adsb.lol**: when Flightradar24 fails twice in a row, or turns the panel away, the panel switches to adsb.fi on its own, and to adsb.lol if adsb.fi is out too. adsb.lol is read at most every 3 minutes. The panel goes back to Flightradar24 once its wait is over, longer each time it fails again, and as long as it asks when it turns the panel away. A read from adsb.fi is larger, about 30 KB for 50 planes, about 5 MB an hour.
- **Routes**: for an airline flight whose source has no full route (adsb.fi, adsb.lol, your own receiver, and some Flightradar24 flights), the panel looks the route up from adsb.im, at most every 30 seconds, over plain HTTP, or from adsbdb while adsb.im is failing or turning the panel away. It shows a route only when it fits where the plane is and the way it's heading, and keeps each route for 3 hours.
- **Nothing while it's off screen**: the Flight Tracker reads only while it's on the main board or in the second area. Off screen, the device page says it isn't reading planes, rather than an old count.
- **Near the 180th meridian**: Flightradar24's box can't reach across it, so for a home whose circle crosses it, like in Fiji, the panel reads adsb.fi first. The card's line then names adsb.fi without saying Flightradar24 is out.

The card's line says which source the planes on the panel came from.
:::

## Your own receiver

:::note[Coming in the next release]
With an ADS-B receiver on your network, the panel reads it first, every 10 seconds. When it fails twice in a row, the panel reads Flightradar24 and the backups above until the receiver answers again. A changed address is read right away.

The receiver's file lists every plane it hears, some with no recent position; those aren't counted as nearby.

- A bare address, like **192.168.1.50** or **piaware.local**, reads tar1090's planes at `/tar1090/data/aircraft.json`.
- A whole address ending in `.json` is read as it is, like `http://192.168.1.50/skyaware/data/aircraft.json` for PiAware's SkyAware or `http://192.168.1.50/dump1090-fa/data/aircraft.json`.
- readsb's own API, an address with port 30152, `/re-api` or a `?` in it, gets the circle around your home added.

The receiver's planes need no internet, but their routes still come from adsb.im or adsbdb.
:::

## Your location

:::note[Coming in the next release]
The home location stays on the panel, to four decimal places (about 10 meters), with the place's name if you looked one up. Each read sends the flight data service a box around it, not the spot itself: the center is rounded to about a kilometer first, and the box reaches a little past your **Within** distance all around it. adsb.fi and adsb.lol get that rounded center and the distance. The panel still works out each plane's distance and direction from your exact spot. Route lookups send only the planes' callsigns and positions, over plain HTTP to adsb.im.

Looking up a town or a ZIP code goes from your browser to Open-Meteo's place search, not from the panel. **Clear** removes the location from the panel, and a [factory reset](/using/buttons-and-resets/) does too.
:::
