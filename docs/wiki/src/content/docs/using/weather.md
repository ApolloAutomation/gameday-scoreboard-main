---
title: "Weather"
description: "The weather at home on the main board or in the second area: now, the next hours and days, an animated radar, air quality and, in the US, the National Weather Service's warnings."
sidebar:
  order: 10.9
---

Weather is a display the panel can show in place of the scoreboard: the weather at the home you set for the [Flight Tracker](/using/flights/), page by page. Pick it for the main board under **Showing** in the Panel card on the device page, or for the [second area](/using/panel-sizes/#the-second-area) under **Shows**. There's nothing to sign up for: the weather comes from free services that need no account or key.

Home Assistant's entities keep following the scoreboard's game while Weather is on the panel.

## Set it up

The device page has a **Weather** card. Weather and the Flight Tracker share one home: set it in either card and both use it.

1. Under **Home location**, type a town, a ZIP or postal code, or a latitude and longitude like **40.64, -73.78**, and tap **Find**.
2. For a town or a code, your browser looks it up and lists what it found, like **Brooklyn, New York, United States** and **Brooklyn, Iowa, United States**. Tap the right one. When there's only one, it's picked for you.
3. Choose **Weather** under **Showing** in the Panel card, or under **Shows** for the second area.

**Home** shows the spot the panel has, with **Clear** to remove it (from the Flight Tracker too). The line at the bottom of the card says what the panel has, like **31°F, Cloudy, from Open-Meteo. 1 alert: Winter Storm Warning. Air quality 41.**

The panel uses your time zone's units: Fahrenheit in the US time zones and Celsius elsewhere, and wind in mph in the US and UK time zones and km/h elsewhere.

**Weather warnings in the ticker** is a switch, off to start. See [Warnings in the ticker](#warnings-in-the-ticker).

## What the panel shows

Weather takes turns through its pages, about 10 seconds each, starting over each time it comes on screen:

- **Warning** (12 seconds, first, only while the National Weather Service has an alert for home): the alert's name in its color, red for a warning, orange for a watch, yellow for an advisory and blue for a statement, and when it ends, like **Until 10a Sat**. With more than one, the most urgent shows, and **+1 more** names the next. On 128 wide and more a warning sign sits at the top right, and on three or four panels in a row and the full screen of two rows of two, the alert's own words follow, as many lines as fit.
- **Now**: the temperature in big digits, colored from blue when it's cold to orange when it's hot, a picture of the sky (sun, moon, partly cloudy day or night, clouds, fog, drizzle, rain, heavy rain, thunder, snow, sleet, or wind when it blows hard on a dry day), the sky in words, today's high and low, the wind and where it comes from, the humidity and what it feels like.
- **Hours**: the next hours with a picture, the temperature and the chance of rain or snow when it's 10% or more. On two, three or four panels in a row, a line under them traces the temperature hour by hour.
- **Days**: today and the days after with a picture, the high, the low and, where there's room, the chance of rain or snow.
- **Radar**: the rain and snow around home on a dark map, about 150 miles (240 km) across in the middle of the US, with home as a white dot in the middle and a gray ring at a round distance, like **25mi**, at the bottom right. It plays the last hour, a frame every half second, and holds the newest for two seconds. The time of the frame on screen is at the top left, white for the newest. Until the first frame comes in it says **Loading radar**.
- **Air quality** (8 seconds): the US Air Quality Index in big digits, in the color of its band, the band's name (Good, Moderate, Unhealthy for sensitive groups, Unhealthy, Very unhealthy or Hazardous), the pollutant behind it, like **Mostly Ozone**, and the fine particles (PM2.5) in micrograms per cubic meter.

| Area | What it shows |
| --- | --- |
| 64x64 (one panel, or the right panel of three wide) | **Now**: the picture and the temperature at the top, then the sky, the high and low, the wind and humidity, and what it feels like. **Hours**: a row every three hours for the next 12. **Days**: a row a day for five days. **Radar**: the whole area. |
| 128x64 (one 128x64 panel, the right two panels of four wide, either half of two rows of two) | **Now**: a bigger picture and temperature, the high, low and humidity beside them, the sky in words, then what it feels like and the wind. **Hours**: six, two hours apart. **Days**: six. **Radar**: the whole area. **Air quality**: the number on the left, its band and pollutant on the right. |
| Three or four panels in a row, with no second area | The 128-wide pages with more beside them: **Now** adds the next sunrise and sunset and the air quality, and on four panels today's chance of rain and the gusts. **Hours**: eight, two hours apart on three panels, three on four. **Days**: seven. **Radar**: the map on the left, now in brief and the radar's source beside it, and on four panels the next hours' chance of rain. **Air quality**: the whole scale, its band lit. |
| Two rows of two with **Full screen** on | **Now**: the picture three times its size, the temperature, the high and low, the sky in big letters and a grid of what it feels like, the humidity, the wind, the gusts, sunrise, sunset, the chance of rain and the air quality. **Hours**: two rows of four, three hours apart, with pictures twice their size. **Days**: a row a day for seven days in bigger letters. **Radar**: all 128 by 120. **Warning**: the alert's words below it. |

The ticker along the bottom keeps the same words through the pages: any alerts first, in their colors, with what each one says, then home's temperature, sky and what it feels like, today's high and low with the chance of rain, sunrise and sunset, the wind and gusts, the humidity and the air quality in its band's color.

The panel shows a message instead when it has no weather to show:

- **Set your home on the device page**: there's no home yet.
- **Loading weather**: the first answer hasn't come in yet, or the home just changed.
- **Can't reach weather data**: three forecast reads in a row failed, and the panel has no forecast from the last 3 hours. The panel keeps trying.

At night, with [night hours](/using/brightness-and-clock/#night-hours) on, the clock takes Weather's place until they end, and the device page says so.

With the **Idle display** on **Off or dim** and Weather on the main board, the panel stays dark or dim except while the National Weather Service has a warning or a watch out for home. It keeps reading while it's dark, so an alert lights it.

## Warnings in the ticker

With **Weather warnings in the ticker** on in the Weather card, the National Weather Service's warnings and watches for home lead the main board's ticker in red or orange whatever it shows, the scoreboard or any display, like **Winter Storm Warning until 10a Sat**. Advisories and statements stay on Weather's own pages. While it's on, the panel checks for alerts every 5 minutes, even with Weather off the panel; it reads nothing else for it. With it off, as it starts, the panel checks only while Weather is on screen.

This needs a home in the US: the National Weather Service covers the 50 states and the territories. For a home elsewhere the panel never asks.

## Where the data comes from

- **Forecast**: [Open-Meteo](https://open-meteo.com) (api.open-meteo.com), about every 10 minutes while Weather is on screen: the weather now, the next 24 hours and the next 7 days, about 2 KB a read. When it fails three times in a row, or turns the panel away, the panel reads [MET Norway](https://api.met.no)'s forecast (api.met.no) instead, about 40 KB, and goes back to Open-Meteo after 30 minutes, longer each time it fails again. MET Norway's forecast has no chance of rain and no sunrise or sunset, so those leave the pages while it's in use.
- **Air quality**: Open-Meteo's air quality service (air-quality-api.open-meteo.com), about every 30 minutes, under 1 KB. Its page shows only with an answer from the last 2 hours.
- **Alerts**: the National Weather Service (api.weather.gov), every 5 minutes, for a home in the US. When it says home isn't in its area, the panel stops asking for that home.
- **Radar**: the list of the last radar frames from [RainViewer](https://www.rainviewer.com) (api.rainviewer.com), every 10 minutes, then each frame's image (tilecache.rainviewer.com), about 20 KB. A frame is downloaded once and kept while it's in the last hour, so after the first minute on screen the panel downloads about one image every 10 minutes. When RainViewer or its images can't be reached and home is in the US, the radar comes from Iowa State University's Iowa Environmental Mesonet (mesonet.agron.iastate.edu), the National Weather Service's NEXRAD radars put together, a few KB a frame. Outside the US, with RainViewer out, the radar page is left out until it's back. When the images won't load from either, the radar page is left out for 10 minutes before the panel tries again.
- **Nothing while it's off screen**: Weather reads only while it's on the main board or in the second area, besides the alert checks above when the switch is on. Off screen, the device page says it isn't reading.

When a service fails, the panel waits longer each time before asking it again, and as long as it asks when it turns the panel away.

## Your location

Weather uses the home the Flight Tracker keeps on the panel. Each weather service gets that spot rounded to two decimal places, about a kilometer, never the exact one. Radar images are centered on that rounded spot too. Looking up a town or a ZIP code goes from your browser to Open-Meteo's place search, not from the panel. **Clear** removes the home from the panel, and a [factory reset](/using/buttons-and-resets/) does too.
