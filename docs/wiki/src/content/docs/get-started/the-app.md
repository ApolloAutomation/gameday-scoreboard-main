---
title: "The Game Day app"
description: "What the separate Game Day app does, what the panel's QR codes open instead, and how to do everything without it."
sidebar:
  order: 4
---

The Game Day app is a companion app for the panel, for iPhone and Android. It has no account and no cloud service. The app talks to the panel directly, over Bluetooth during setup and over your Wi-Fi after that.

:::caution
The Game Day app is a separate project. Apollo Automation does not maintain it, and it may not find panels running this firmware, which names itself to the network as an Apollo Automation project. The panel doesn't send anyone to it: nothing on its screen, its ticker or its device page points to the app. You don't need it: the panel's hotspot and its device page do everything the app does. See [Without the app](#without-the-app).
:::

## What the app does

- **Finds your panels.** A new panel shows up as a new panel nearby, over Bluetooth. A panel that's already on your Wi-Fi shows up under the panels found on your network.
- **Sets a new panel up over Bluetooth.** The panel sends the app the Wi-Fi networks it can see, strongest first. You pick yours and enter the password. See [Set up your panel](/get-started/setup/).
- **Picks your team** and the mode, the same choices as the device page. See [Pick your team](/get-started/pick-your-team/).
- **Changes the panel's settings**: the ticker, celebrations, favorites, brightness, the remote and the rest. The app's screens follow the device page's cards, so a setting is in the same place in both.

## If the app doesn't list your panel

Add it by address. Tap the add button and enter the panel's name, like `gameday-2f6a70.local`, or its IP address, like `192.168.1.50`. The panel shows its name after it joins Wi-Fi, and its ticker shows the IP address until you pick a team. To see both any time, hold the button on the controller for about 1.5 seconds.

The phone has to be on the same Wi-Fi network as the panel. A guest network can keep the two apart. More in [Troubleshooting](/help/troubleshooting/).

## The QR code on the panel

The panel's QR codes don't open the app or its project's page, so you never need the app to use them. During setup, and once the panel's own hotspot comes up a few minutes into a Wi-Fi outage, the QR code joins that hotspot. Once the panel is on your Wi-Fi, it opens the panel's device page. The camera of any iPhone or Android phone reads both. Where the app does find a panel, it sets it up over Bluetooth, with no QR code.

## Without the app

You don't need the app to use the panel.

- **To put it on Wi-Fi**, join the panel's own hotspot: scan the QR code on the panel with your phone's camera, or pick the hotspot from the Wi-Fi list on any phone or laptop. It's named **Game Day** plus six letters and numbers, like `Game Day 2f6a70`, and a setup page opens when you join it. See [Set up your panel](/get-started/setup/#use-the-panels-hotspot).
- **To pick your team and change settings**, open the panel's device page in any browser on the same Wi-Fi: scan the QR code the panel shows once it's on your Wi-Fi, or open the `gameday-xxxxxx.local` name or IP address the panel shows. Everything the app sets is there. See [The device page](/using/device-page/).
