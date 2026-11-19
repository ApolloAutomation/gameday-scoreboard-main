---
title: "Build it yourself"
description: "Compile the Apollo Pulse firmware from its YAML with ESPHome, flash it over USB, and run the host tests."
sidebar:
  order: 5
---

Build from the YAML when your board has its own pins, when you want to change something, or when you want your own keys on the panel. Everything you need is in the [firmware repo](https://github.com/ApolloAutomation/gameday-scoreboard-main).

## What's in the repo

| Path | What it holds |
| --- | --- |
| `firmware/gameday.yaml` | The release build for the MoonHub75 pinout, which the Apollo M-1 (Rev6) shares. It names the build and picks the pin map. |
| `firmware/gameday-2panel.yaml` | `gameday.yaml` starting two panels wide. The M-1 installer offers it for a 128x64 display. |
| `firmware/gameday-scoreboard75.yaml` | The release build for the scoreboard75 controller board. Not for other boards. |
| `firmware/gameday-bar.yaml`, `firmware/gameday-scoreboard75-bar.yaml` | Builds for panels in bars, which follow the beta versions and install them on their own at night. Never released; don't flash them on your own panel. |
| `firmware/gameday-demo.yaml`, `firmware/gameday-scoreboard75-demo.yaml` | Test builds with a scripted demo. Never released; don't flash them on a panel you use. |
| `firmware/controllers/` | One pin map per board: `moonhub75.yaml` and `scoreboard75.yaml`. |
| `firmware/gameday-common.yaml` | Everything else: version, update feed, Wi-Fi, the panel layout, the pages and settings. Don't build it directly. |
| `firmware/pages/` | The pages drawn on the panel. The scoreboard is `gameday-live.yaml`. |
| `firmware/web/` | The device page. |
| `components/` | The firmware's own ESPHome components. |
| `tests/` | Host tests for the parser and game logic, and browser tests for the device page. |

The components in `components/` are:

- `gameday`: ESPN fetching, parsing and game logic, in C++.
- `panel_layout`: the panel layout (one to four panels wide, or a 2x2 and how its rows are mounted), applied at boot from a saved choice, so one binary serves every layout.
- `improv_ble`: Bluetooth Wi-Fi setup for the Game Day app.
- `saved_networks`: the up to five Wi-Fi networks the panel remembers.
- `crash_report`: keeps a record of the last crash, freeze or power dip restart in flash, with the log and backtrace of a crash or freeze, for `/gameday/crash`.

`gameday-common.yaml` also pulls in [lvgl-page-manager](https://github.com/pavlov-net/lvgl-page-manager) and [lvgl-canvas-fx](https://github.com/pavlov-net/lvgl-canvas-fx) at fixed versions, and the theme and clock packages from [hub75-studio](https://github.com/pavlov-net/hub75-studio), pinned to a commit so a build always gets the same thing.

## Install ESPHome

Use the ESPHome version CI builds with. It's `ESPHOME_VERSION` at the top of `.github/workflows/build.yml` in the code you clone, 2026.10.2 for the latest release. Install it in a Python virtual environment.

```sh
python -m venv esphome-venv
```

On macOS or Linux:

```sh
source esphome-venv/bin/activate
```

On Windows:

```powershell
esphome-venv\Scripts\activate
```

Then install the pinned version:

```sh
pip install esphome==2026.10.2
```

The `dev` and `beta` branches may build on an ESPHome beta. `main`, which every release and the update feed come from, builds only on a final ESPHome release. CI checks it: a pull request into `main`, or a release tag, fails when the pin isn't a final release like 2026.10.0. A maintainer can let an ESPHome beta through with the `esphome-beta-ok` label on the pull request that promotes it.

## Get the code

Clone `main`, which holds the latest release:

```sh
git clone --branch main https://github.com/ApolloAutomation/gameday-scoreboard-main
```

To build one particular release, put its tag in place of `main`, like `--branch v2026.11.7`. The default branch, `dev`, is where new work lands before a release. It may need an ESPHome beta, and a build from it shows the last release's version number.

```sh
cd gameday-scoreboard-main
```

## Compile

```sh
esphome compile firmware/gameday.yaml
```

Use `compile` to check a change, not `config`. `config` doesn't catch C++ errors. For a 128x64 display, compile `firmware/gameday-2panel.yaml` instead. It has its own build folder, so switching between the two keeps both caches.

## Flash over USB

Plug the board in and run:

```sh
esphome run firmware/gameday.yaml
```

ESPHome builds the firmware and asks which port to flash. Pick the USB port. Joining Wi-Fi and picking a team then work the same as after the [M-1 installer](/diy/install/).

:::caution
A build that keeps the variant `moonhub75` reads the MoonHub75 release feed. When the next release comes out, the device page offers it, and installing it replaces your build. For a board with its own pins, give your copy its own `variant` name. See [Other boards](/diy/boards/#other-boards).
:::

## Your own keys

The prebuilt firmware has no password on updates, no key on the ESPHome API, and an open device page, because the panel trusts your home network. If you want a key or a password on your own panel, add it to your copy of the YAML and build that. Pull requests that add them to the default build aren't merged.

## Host tests

The parser and game logic have host tests. They need g++.

```sh
make -C tests
```

The device page has browser tests against a mock panel, in `tests/web`. They need Node and Python 3. Every command here runs in `tests/web`, not at the top of the repo:

```sh
cd tests/web
```

Run these once:

```sh
npm ci
```

```sh
npx playwright install chromium
```

Then run the tests:

```sh
npm test
```

## Edit the device page

The device page is plain HTML and JavaScript in `firmware/web`. The firmware embeds a generated bundle, so after you edit `app.js`, `app.css` or `demo.js`, rebuild it:

```sh
python3 scripts/build_web.py
```

That rewrites `firmware/web/bundle.js` and `firmware/web/bundle-demo.js`. A pull request commits both with the change, since CI fails when they don't match the sources.

## Generated files

A few files in `components/gameday` are made by scripts. Don't edit them by hand: change the script's source and run it again.

| File | Made by |
| --- | --- |
| `teams.h` | `scripts/build_teams.py` |
| `team_colors.h` | `scripts/build_team_colors.py` |
| `timezones_parsed.h` | `scripts/build_timezones.py` |
| `circuits.h` | `scripts/build_circuits.py` |
| `flight_airlines.h` | `scripts/build_airlines.py` |
| `sprites.h` | `scripts/build_sprites.py`, from the play animations' art in `firmware/art/sprites/*.txt` |

:::note[Coming in the next release]
`apollo_logo.h`, Apollo on the start-up screen, is made by `scripts/build_apollo.py` from `firmware/art/apollo/apollo.png`.
:::

## Size budget

`firmware/budget.json` holds a ceiling for each release build's flash and internal RAM. CI's **Size budget** job fails a pull request that goes over one, or that grows a build by more than the file allows in one go. A change that needs more raises that one entry in the same pull request and says why. Your own copy can be as big as your board allows, but a pull request back to the project has to fit.

## Contributing

Bug reports, test fixtures from odd games, pin maps for new boards and device page fixes are all welcome. [CONTRIBUTING.md](https://github.com/ApolloAutomation/gameday-scoreboard-main/blob/dev/CONTRIBUTING.md) says where help is wanted, how to branch and what CI checks.
