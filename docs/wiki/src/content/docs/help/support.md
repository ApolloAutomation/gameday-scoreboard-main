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
- **The controller.** An Apollo M-1, the scoreboard75 controller board, or the board you built it on.
- **The panel layout.** Which picture is picked under **Matrix** in the Setup card: **One 128x64 panel** or **One 64x64 panel**.

A photo of the panel showing the problem helps too.

### The crash report

If the panel restarted by itself, after a crash, a freeze or a power dip, it keeps a crash report about the last one, even across an unplug, until the report is cleared. Open `http://<your panel's address>/gameday/crash` in a browser, like `http://gameday-2f6a70.local/gameday/crash`, and paste what it shows into your issue. A `count` of 0 means no crash is saved. Otherwise `count` is how many such restarts there have been since the report was last cleared.

Built from source? In the firmware repo, `python3 scripts/crash_report.py <panel address>` prints the same report and turns its addresses into function names, files and line numbers. It decodes against the firmware of the matching release on GitHub, or against your own build with `--elf` and the path to its `firmware.elf`, using the tools that compiling the firmware installs. Add `--clear` to empty the panel's report after printing it.

## Orders and returns

For orders, shipping and returns, go to the store at [gameday-scoreboard.com](https://gameday-scoreboard.com).
