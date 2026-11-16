---
title: "Install the firmware"
description: "Flash the Game Day Scoreboard firmware onto an Apollo M-1 or another MoonHub75-pinout board from your browser, and how updates reach it."
sidebar:
  order: 4
---

The [Apollo M-1 and MoonHub75 installer](https://apolloautomation.github.io/gameday-scoreboard-main/install/diy/) flashes the prebuilt firmware for the MoonHub75 pinout, which the Apollo M-1 (Rev6) shares, from your browser. You don't need ESPHome or any other software.

:::caution
Installing erases whatever firmware was on the board. This installer is for boards on the MoonHub75 pinout only, the Apollo M-1 Rev6 among them. If you have a scoreboard75 controller board, pick it on the [installer on the home page](https://apolloautomation.github.io/gameday-scoreboard-main/) instead. The home page also offers the Apollo M-1 builds.
:::

## Before you start

- A desktop computer with Chrome or Edge. The installer talks to the board over USB, and the page tells you if your browser can't.
- A USB cable from the computer to the board.
- Your panel wired to the board. [Panels and power](/diy/panels/) covers it.

## Install

1. Open the [Apollo M-1 and MoonHub75 installer](https://apolloautomation.github.io/gameday-scoreboard-main/install/diy/) in Chrome or Edge.
2. Click the picture that matches your panels: **One 64x64 panel**, or **One 128x64 panel** (or two 64x64 side by side). This sets the layout the scoreboard starts with. The Install button appears once you pick.
3. Plug the board into the computer over USB.
4. Click **Install for Apollo M-1 / MoonHub75** and pick the board's port when the browser asks.
5. When the install finishes, enter your Wi-Fi details in the dialog that follows.

The two pictures install two builds of the same firmware. The 64x64 choice installs the build from `firmware/gameday.yaml`. The 128x64 choice installs the build from `firmware/gameday-2panel.yaml`, which is the same firmware starting two panels wide. You can change the layout later on the device page either way. For three or four panels, pick **One 64x64 panel** here, then **Three wide**, **Four wide** or **Two rows of two** on the device page once the panel is on Wi-Fi. See [Panels and power](/diy/panels/#one-to-four-panels).

## If you skip the Wi-Fi dialog

If you close the dialog, the panel starts a hotspot named Game Day plus six letters and numbers, like Game Day 2f6a70, and shows a QR code that joins it. Scan the code with your phone's camera, or join the hotspot from any phone or laptop, and a setup page opens. The panel only joins 2.4 GHz networks.

## Set it up

From here a board you built works like any Game Day Scoreboard. Follow [Set up your panel](/get-started/setup/) to finish joining Wi-Fi, then [Pick your team](/get-started/pick-your-team/).

## How updates work

A DIY board gets every release, built for its pinout, from the same release feed as a Game Day Scoreboard. It checks for a new release every 6 hours. When one is out, an Install button appears on the device page and the update goes over Wi-Fi. Turn on **Install updates automatically** and the panel installs a new release on its own between 3 and 5 AM, never while a game it follows is live. It's off unless you turn it on. See [Updates](/using/updates/).

A **Follow beta** switch in the Device card gets test versions of the next release before everyone else. They can have bugs. A beta is named for a coming release, plus a build number, like `2026.11.9-beta.20`. With **Follow beta** on, the panel is offered each newer beta, to take with **Install**, or overnight with **Install updates automatically** on. Turn it on while the panel runs a release and it waits for a beta of the next release; until one is out, the Firmware box says **You're ahead of the latest beta**. Turn it off while the panel runs a beta and it waits for the next release. Once that release ships, the Firmware box offers it with **Install**, and the panel installs it overnight with **Install updates automatically** on.

Each build has a variant name, and the variant names the folder the panel reads updates from:

| Variant | Build | Used for | Updates from |
| --- | --- | --- | --- |
| `moonhub75` | `firmware/gameday.yaml` | M-1 installer, 64x64 choice | `moonhub75` |
| `moonhub75-2panel` | `firmware/gameday-2panel.yaml` | M-1 installer, 128x64 choice | `moonhub75` |
| `scoreboard75` | `firmware/gameday-scoreboard75.yaml` | The scoreboard75 controller board only | `scoreboard75` |

The 128x64 build reads its updates from the `moonhub75` folder too. The panel saves its layout the first time it starts, so that update keeps it two panels wide.

Each release on [GitHub](https://github.com/ApolloAutomation/gameday-scoreboard-main/releases) also carries two factory images, `gameday-scoreboard-<version>.factory.bin` for the MoonHub75 pinout and `gameday-scoreboard-<version>-scoreboard75.factory.bin` for the scoreboard75 controller board, and a zip of each variant's folder.
