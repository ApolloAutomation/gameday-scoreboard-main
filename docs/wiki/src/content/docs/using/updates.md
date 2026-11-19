---
title: "Firmware updates"
description: "How the panel checks for new firmware, installing an update from the device page, and installing updates automatically overnight."
sidebar:
  order: 13
---

The panel updates its own firmware over Wi-Fi. There is nothing to plug in. What changed in each release is on the [What's new](/changelog/) page.

## How the panel checks

The panel reads its updates from the Apollo Automation release feed, on the project's site at `apolloautomation.github.io/gameday-scoreboard-main`. It checks for a new release every 6 hours, and once a few seconds after it first sets its clock when it starts. **Check for updates** in the Device card on the device page checks right away.

The **Firmware** box in the Device card says what it found:

| The box says | What it means |
| --- | --- |
| **You're up to date** and the version | The panel runs the newest release, or the newest beta with **Follow beta** on. |
| **Update available** and the version | A newer release is out, or a newer beta with **Follow beta** on. The box lists what's in it, with a **Release notes** link, and shows an **Install** button. |
| **You're ahead of the latest release** and the version, or **You're ahead of the latest beta** with **Follow beta** on | The panel runs something newer than the newest release, or the newest beta with **Follow beta** on, which the box names under it, like a test build. Nothing to install. With betas, see [Beta versions](#beta-versions). |
| **Not checked yet** | The panel hasn't heard from the update server since it started. Tap **Check for updates**. |
| **The update to** a version **didn't take** | That version crashed as it started, and the panel went back to the one it had. See [An update that didn't take](#an-update-that-didnt-take). |

For a release, the **Release notes** link in the box opens that release's section of the changelog on the project's site.

If the check can't reach the update server, the page says **Could not reach the update server. Try again in a minute.**

## Installing an update

1. Open the device page and tap **Install** in the Firmware box.
2. Confirm. The panel downloads the update and restarts.

When an update is out, a bar at the top of the device page says **Update available** and the version (or **Update didn't take** or **Update didn't install** for one that failed), with **Install** and **Later**. **Install** asks to confirm, just like the button in the Firmware box. With **Install updates automatically** on, the bar and the Firmware box add **It installs on its own between 3 and 5 AM.** **Later** hides the bar for that version in that browser; a newer version brings it back, and the Firmware box still offers it. While an update installs, the bar says **Installing** and the version, **Keep the panel powered.**, and the panel itself says **Updating**, **Keep it plugged in** and **Back in a minute** in big words across its whole width (**Updating**, **Keep it**, **plugged in** on a single 64x64 panel) until it restarts on the new version. If the install doesn't go through, the panel goes back to what it was showing.

The Device card, where the Firmware box is, starts folded. When an update is out, or one didn't take, the card opens by itself with **Install** in sight, unless you've opened or folded it yourself in that browser. While it's folded, the words beside **Device** say **Update available** and the version, **Update didn't take**, or **Installing** and the version: tap **Device** to open it. See [The Device card](/using/device-page/#the-device-card).

While it installs, the box says **Installing** and the version, with **Keep the panel powered. It reboots when done and this page reconnects.** Leave the panel plugged in until it's back. The page reloads by itself once the panel answers on the new version.

If the power goes out or the download fails partway through, the panel keeps running the firmware it had. A new version only starts once it's fully downloaded and checked.

If an install doesn't start, or its download fails while the panel stays on, the page says so as soon as the panel answers, instead of waiting for a restart that isn't coming. The Firmware box says **The update to** that version **didn't install**, with **The panel is still on** the version it runs **. Install tries again.** under it, and **Install** is back. While the Device card is folded, the words beside **Device** say **Update didn't install**. The panel also checks for the newest version again right away, so the next **Install** takes the latest one.

You can install an update at any time, during a game too. Your team, favorites and settings stay as they were.

## Install updates automatically

**Install updates automatically** in the Device card is off unless you turn it on. With it on, the panel installs a new release by itself overnight:

- only between 3 and 5 AM in the panel's timezone
- each panel at its own minute in the first hour, so a house with several panels doesn't download all at once
- never while a game the panel follows is live: it waits, and the second hour leaves room for a late game to finish
- one try a night; if that try fails, it tries again the next night

A new release waits a night. The panel doesn't install a release on its own in the first 3 to 5 AM window after it sees that release, only in a later one, so there's time to pull a release that has a problem before panels take it by themselves. A release that comes out in the evening installs the night after next. **Install** on the device page still installs it right away, and betas with **Follow beta** on don't wait.

With the switch off, the panel still checks and the device page tells you when an update is out, but nothing installs until you tap **Install**. Home Assistant has the same switch; see [Home Assistant (optional)](/using/home-assistant/).

## Uploading a firmware file

The panel's `/update` address, where you can upload a firmware file by hand, works only while the panel's setup hotspot is up: join the hotspot and open `192.168.4.1/update`. On your home Wi-Fi it's off, so a web page opened at home can't install firmware on the panel. **Install** on the device page and Home Assistant's **Firmware** entity work as usual. This is so from v2026.11.9 on.

## An update that didn't take

If a new version crashes as it starts, the panel goes back to the firmware it had. It remembers that version and doesn't install it on its own again, so it never restarts into the same bad update every night. A later release still installs, and **Install** on the device page still tries the one that failed.

While that version is the one on offer, the Firmware box says **The update to** that version **didn't take and the panel went back to** the version it runs now, with **Install tries it again.** With **Install updates automatically** on, it adds **It won't install this one on its own.**

## Beta versions

Beta versions are test builds of the next release, for test panels. They can have bugs. Panels you own run the published releases.

A beta carries the number of a coming release, plus a build number, like 2026.11.11-beta.20. A higher build number is a newer beta, and every beta comes before the release with its number: 2026.11.11 is newer than any 2026.11.11 beta.

The **Follow beta** switch in the Device card points a panel at the beta versions: the Firmware box and **Check for updates** show the latest beta instead of the latest release. When a newer beta is out, tap **Install** to take it. With **Install updates automatically** on too, the panel installs each new beta overnight by the same rules as a release. A beta has no section in the changelog, so its **Release notes** link opens the change on GitHub that it was built from.

Right after you turn **Follow beta** on or off, the Firmware box says **Checking for the latest beta**, or **Checking for the latest release**, until the panel has read that list, usually within a second or two. **Install** comes back then, so an update from the list you just left can't be installed by mistake. If the panel can't read the list, it tries a few more times over the next two minutes, then at its next check, or when you tap **Check for updates**.

Turn **Follow beta** on while the panel runs a release, and it waits for a beta of the next release. The betas of the release it already runs are older, so they aren't offered, and the Firmware box says **You're ahead of the latest beta** until a newer beta is out.

Turn **Follow beta** off while the panel runs a beta, and it stays on that beta. The Firmware box says **You're ahead of the latest release** until the next release ships. Then the box offers that release, and with **Install updates automatically** on, the panel installs it overnight.
