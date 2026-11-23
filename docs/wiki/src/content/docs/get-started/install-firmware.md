---
title: "Install or reinstall the firmware"
description: "Install or reinstall the Apollo Pulse firmware from a desktop browser over USB, and when you would need to."
sidebar:
  order: 5
---

An Apollo M-1 ships with WLED, so you install Apollo Pulse on it once from your browser, as below. A ready-made Apollo Pulse panel on the scoreboard75 controller board comes with the firmware installed. After that the panel updates itself over Wi-Fi. See [Updates](/using/updates/) for the normal way new versions arrive.

## When you need it

- **The panel won't start.** It stays dark or keeps restarting.
- **You want to start over** with a clean install.

## What a USB install erases

A normal update over Wi-Fi keeps your team, favorites, Wi-Fi and every other setting.

A USB install from the browser installer is different. It erases whatever was on the controller and starts fresh, so you set the panel up again afterwards: Wi-Fi, team and settings.

## Install from your browser

You need a desktop or laptop computer with Chrome or Edge, and a USB cable.

### Apollo M-1

1. Open the [Apollo installer for the M-1](https://install.apolloautomation.com/#/m-1).
2. Under **Choose your firmware**, pick **Game Day Scoreboard (Rev6 only)**.
3. Plug the controller into the computer with the USB cable.
4. Click **Connect & Install** and pick the controller's port when the browser asks.
5. Wait for the install to finish.
6. The installer then offers to put the panel on your Wi-Fi. Enter your network name and password.

The panel starts on one 64x64 panel. For a 128x64 panel or more panels, pick the layout on the device page once the panel is on Wi-Fi. See [Panel sizes](/using/panel-sizes/).

### scoreboard75 controller board

1. Open the [installer](https://apolloautomation.github.io/gameday-scoreboard-main/).
2. Click **scoreboard75 controller board**. The Install button appears once you pick.
3. Plug the controller into the computer with the USB cable.
4. Click **Install for scoreboard75**.
5. Wait for the install to finish.
6. The same dialog then offers to put the panel on your Wi-Fi. Enter your network name and password.

If you skip the Wi-Fi step, the panel shows **SETUP** and starts its **Apollo Pulse** hotspot. The setup screen's QR code joins the hotspot: scan it with your phone's camera and the setup page opens. Carry on from [Set up your panel](/get-started/setup/).

The hotspot is named **Apollo Pulse** plus six letters and numbers, like `Apollo Pulse 2f6a70`.

If the installer says the browser can't talk to the controller over USB, switch to Chrome or Edge on a desktop computer.

## Which controller do I have?

The Apollo M-1 is Apollo Automation's controller, with Rev6 printed on the back. The scoreboard75 is the board built into the ready-made panel that comes with a 128x64 panel. Picking the wrong one installs firmware for other pins, and the panel stays dark; install again with the right choice.

A MoonHub75 board uses the Apollo M-1 choices. For your own ESP32-S3 board, see [Install on your own board](/diy/install/).
