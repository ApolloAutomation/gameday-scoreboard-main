---
title: "Get support"
description: "How to get help with Game Day Scoreboard on GitHub, what to include in an issue, and where to find the crash report."
sidebar:
  order: 5
---

Try [Troubleshooting](/help/troubleshooting/) and [What the panel is telling you](/help/panel-messages/) first. Most problems have a fix there.

## Open an issue on GitHub

Support is on GitHub. [Open an issue](https://github.com/ApolloAutomation/gameday-scoreboard-main/issues) and pick the bug or feature form. Bug reports from any panel are welcome, whether it's a Game Day Scoreboard or a board you built. Issues are public, so leave out Wi-Fi passwords.

Include what you can of this:

- **What happened.** What the panel or the app was doing, and what it said on the screen.
- **The firmware version.** It's in the Device card on the device page, next to **Firmware**.
- **The controller.** The Game Day Scoreboard controller, or the board you built it on.
- **The panel layout.** Which picture is picked under **Matrix** in the Setup card: **One 128x64 panel** or **One 64x64 panel**.

A photo of the panel showing the problem helps too.

:::note[Coming in the next release]
If the panel restarted by itself, it keeps a crash report until it's cleared, even across an unplug. Open `http://<your panel's address>/gameday/crash` in a browser, like `http://gameday-2f6a70.local/gameday/crash`, and paste what it shows into your issue. A `count` of 0 means it hasn't restarted unexpectedly.

Built from source? `python3 scripts/crash_report.py <panel address>` in the firmware repo prints the same report and turns its addresses into file and line numbers, using the matching release's firmware or `--elf` for your own build.
:::

## Orders and returns

For orders, shipping and returns, go to the store at [gameday-scoreboard.com](https://gameday-scoreboard.com).
