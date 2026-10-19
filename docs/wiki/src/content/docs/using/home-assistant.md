---
title: "Home Assistant (optional)"
description: "The panel is an ESPHome device: add it to Home Assistant to see the game status and automate power, brightness, the team and updates."
sidebar:
  order: 16
---

Nothing about the panel needs Home Assistant. It reads ESPN, runs its own device page and updates itself over Wi-Fi. If you already use Home Assistant, you can add the panel to it, because the firmware is an ordinary ESPHome device.

## Adding it

The panel joins Home Assistant through the ESPHome integration, which comes with Home Assistant. Put the panel on your Wi-Fi first (see [Set up your panel](/get-started/setup/)).

1. With Home Assistant on the same network, it discovers the panel as a new ESPHome device. A notification says a new device was discovered, and **Settings → Devices & services** lists it under **Discovered**.
2. Press **Add** (or **Configure**) and confirm. The prebuilt firmware has no API encryption key, so there is no key to enter.
3. Pick an area for the panel if you like, and finish.

This button opens Home Assistant at **Settings → Devices & services**, where a discovered panel waits:

[![Open your Home Assistant instance and show your integrations.](https://my.home-assistant.io/badges/integrations.svg)](https://my.home-assistant.io/redirect/integrations/)

Not discovered? Add it by its address instead. Press **Add integration**, pick **ESPHome**, and enter the panel's `gameday-xxxxxx.local` name (with your panel's six characters) or its IP address. Leave the port at 6053. This button opens the same window:

[![Open your Home Assistant instance and start setting up a new integration.](https://my.home-assistant.io/badges/config_flow_start.svg)](https://my.home-assistant.io/redirect/config_flow_start/?domain=esphome)

Once it's added, the panel shows up as a device under the ESPHome integration, with the entities below. This button opens that integration:

[![Open your Home Assistant instance and show an integration.](https://my.home-assistant.io/badges/integration.svg)](https://my.home-assistant.io/redirect/integration/?domain=esphome)

Picking a team or a mode from Home Assistant counts as setting the panel up, the same as the device page, so the ticker stops asking you to pick a team.

Want to change the firmware itself, for example to add your own API key? Build it from the YAML with ESPHome. See [Build it yourself](/diy/build-from-source/).

## What appears

| Entity | What it is |
| --- | --- |
| **Team** | The team the panel follows in My team. Every league except men's college basketball, whose list would be too long for one entity; pick those teams on the device page. |
| **Mode** | My team, Favorite teams or Live games, plus the older Live NFL, Live college and Live anything. |
| **Power** | The panel's lights on or off. |
| **Brightness** | 1 to 255. |
| **Scroll Speed** | The ticker speed, 1 to 10. |
| **Select Page** | The Scoreboard or the Clock page, with **Page Next** and **Page Previous** buttons. |
| **Game Status** | The game in one line: the matchup, whether it's upcoming, live or final, and the score. |
| **Last Play** | ESPN's latest play. |
| **Firmware** | The firmware update, so Home Assistant shows when a new release is out. |
| **Install Updates Automatically** | The same switch as on the device page. See [Firmware updates](/using/updates/). |
| **Check for Updates**, **Refresh Now**, **Reboot** | The device page's buttons. |
| **Reset Wi-Fi**, **Factory Reset** | The resets. See [The boot button and resets](/using/buttons-and-resets/). |
| **WizMote Auto-Discovery**, **WizMote MAC Address**, **WizMote Status**, **Clear WizMote Pairing** | Pairing a WizMote. See [WizMote remote](/using/remote/). |
| **IP**, **ESPN Data** | The panel's IP address, and how much it has downloaded from ESPN since it started. |

A few diagnostic sensors, the Wi-Fi signal and the free memory readings, are there but turned off until you turn them on in Home Assistant.

Favorites, the timezone, the ticker switches, the celebrations, the Live games leagues and the panel layout have no entities. Set them on the device page or in the app.

:::note[Coming in the next release]
A **Chip Temperature** sensor reports the controller's temperature, the reading the panel uses to dim itself when it runs hot.
:::

## What you can automate

Game Status changes when a game starts, when someone scores and when it ends, not on every play, so it makes a good trigger and keeps your history small. Some ideas:

- Flash the room lights or play a sound when Game Status changes during your team's game.
- Turn the panel on before kickoff and off at bedtime with **Power**, or turn **Brightness** down at night.
- Switch **Team** or **Mode** for the evening's game.
- Install updates on your own schedule with the **Firmware** entity.

If you want room lighting to celebrate with the score, the separate [gameday-matrix](https://github.com/bharvey88/gameday-matrix) project does the same game tracking through a Home Assistant blueprint, using the same splash rules.

## On your network

The panel treats your home network as trusted: Home Assistant, the app and the device page all connect without a password. Anyone on your Wi-Fi can change its settings. Don't expose it to the internet.
