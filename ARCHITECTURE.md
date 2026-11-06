# Architecture

How Game Day Scoreboard is put together: the builds, the components, the
path a score takes from ESPN to the panel and the device page, the update
channels, and the sites, scripts, CI and tests around them. File and line
references are to `dev` when this was written; follow the names, since line
numbers drift.

## The builds

Seven top-level ESPHome files in `firmware/`. Each is a few lines: it sets
`variant` (which names the update manifest folder), sometimes
`default_panel_cols`, picks a pin map from `firmware/controllers/`, and gets
everything else from `firmware/gameday-common.yaml`.

| File | What it is | Variant | Released |
|---|---|---|---|
| `gameday.yaml` | MoonHub75 pinout (the Apollo M-1 Rev6 shares it) | `moonhub75` | Yes, DIY installer, one panel |
| `gameday-2panel.yaml` | `gameday.yaml` booting two panels wide | `moonhub75` (crash report says `moonhub75-2panel`) | Yes, DIY installer, two panels; updates from `moonhub75` |
| `gameday-scoreboard75.yaml` | scoreboard75 controller board, boots two panels wide | `scoreboard75` | Yes, main installer |
| `gameday-demo.yaml`, `gameday-scoreboard75-demo.yaml` | release build plus `pages/demo.yaml`: a scripted game, no updater | as above | Never |
| `gameday-bar.yaml`, `gameday-scoreboard75-bar.yaml` | release build plus `pages/bar.yaml`: always on the beta channel, installs on its own at night | as above | Beta channel only |

ESPHome 2026.10 builds each into `firmware/.esphome/build/gameday-<variant>`
(with `-2panel`, `-demo` or `-bar`), so switching builds keeps the other
caches. CI builds with the ESPHome version pinned as `ESPHOME_VERSION` in
`.github/workflows/build.yml`.

### gameday-common.yaml

- **Substitutions**: `name`, `friendly_name`, `version` (bumped at release
  time), `site_base` (the project's site, every update URL is built from it),
  `update_source`, `beta_update_source`, panel geometry, and
  `hub75_studio_ref` (the hub75-studio commit the theme and clock packages are
  pinned to).
- **esp32**: a 16 KB loop task stack; Bluetooth and mbedTLS buffers moved to
  PSRAM so TLS has internal RAM; a shorter SNTP start delay.
- **esphome**: the project name `ApolloAutomation.gameday-scoreboard`, and
  `on_boot` steps at priority 300 (apply brightness through the guard), 260
  (name the hotspot "Game Day xxxxxx") and 240 (setup after a Wi-Fi reset,
  Bluetooth grace or Bluetooth on).
- **external_components**: the local `components/` (`gameday`,
  `panel_layout`, `improv_ble`, `saved_networks`, `crash_report`), plus
  `lvgl-page-manager` and `lvgl-canvas-fx` from pavlov-net.
- **packages**: hub75-studio's ESPHome version, theme and clock page, and the
  local pages `boot`, `scoreboard` (`gameday-live`), `wizmote`, `qc` and
  `guard`.
- **Networking**: `wifi` with `on_connect` / `on_disconnect`, `api` (no key,
  `reboot_timeout: 0s`), `ota` (ESPHome and `http_request`), `captive_portal`,
  `improv_serial`, `esp32_ble` off at boot, `improv_ble`, `web_server` v3 with
  the custom page, `http_request` (20 s timeout, `verify_ssl: true`).
- **Updates**: the `http_request` update entity `gd_firmware_update` (every
  6 h, and once 3 s after the first clock sync, since TLS fails before the
  clock is set).
- **gameday**: the component's settings (`beta_channel`, `on_action`,
  `on_update`).
- **Home Assistant entities**: only the ones worth automating (Team, Mode,
  Power, Brightness, Scroll Speed, Select Page, Install Updates
  Automatically, the buttons, Game Status, Last Play, IP, ESPN Data, RSSI and
  disabled heap sensors). Everything else lives in the device page's own
  state document.

### Pages (`firmware/pages/`)

- `boot.yaml`: the boot and setup overlay on LVGL's top layer: starting,
  setup (QR code, hotspot name), joining, Wi-Fi lost, sign-in Wi-Fi, the
  address screen, and the boot button hold (10 s Reset Wi-Fi, 20 s factory
  reset, 30 s cancel). The QR code holds `https://gamedayscoreboard.app/setup`.
- `gameday-live.yaml`: the scoreboard page; `gd_update` draws every field the
  component sends, for 64 and 128 pixel wide layouts, with the splashes and
  fireworks.
- `wizmote.yaml`: WiZ WizMote pairing over ESP-NOW and its buttons.
- `qc.yaml`: the factory check (colors, row walk, readout), started by an NVS
  flag that `scripts/factory_flash.py` sets or by `/gameday/action?do=qc`.
- `guard.yaml`: chip temperature every 15 s into the heat guard, the dimmed
  icon and message.
- `bar.yaml`, `demo.yaml`: the bar and demo builds' differences.

## The components

- `components/gameday`: everything about games. `gameday.cpp` and
  `gameday.h` hold `GamedayComponent` (the main loop logic, the fetch task,
  the HTTP routes, preferences). The parsing and game logic are pure C++ with
  no ESPHome includes, so the host tests compile them directly:
  `espn_parse.cpp/.h`, `espn_parse_impl.h`, `sport_baseball.cpp`,
  `sport_basketball.cpp`, `sport_hockey.cpp`, `sport_soccer.cpp`,
  `leagues.h`, `poll_timing.h`, `offseason.h`, `recap.h`, `favorites.h`,
  `startup.h`, `panel_text.h`, `badge.h`, `set_request.h`, `captive.h`,
  `health.h`, `guard.h`, `auto_update.h`, `update_mirror.h`, `qc.h`,
  `wifi_history.h`, `logo_lru.h`. `logo_cache.cpp` downloads and decodes
  logos. `demo.cpp` is the scripted demo. `teams.h`, `team_colors.h` and
  `timezones_parsed.h` are generated by `scripts/build_teams.py`,
  `build_team_colors.py` and `build_timezones.py`.
- `components/panel_layout`: the saved panel count (one or two 64x64 wide),
  written into the HUB75 display's config before the driver starts.
  Changing it reboots.
- `components/saved_networks`: up to five Wi-Fi networks (ESPHome keeps
  one), handed to the Wi-Fi component at boot, newest first. The list logic
  is `network_list.h`, tested on the host. Also holds the "Reset Wi-Fi" setup
  flag.
- `components/crash_report`: after a panic, watchdog or brownout restart,
  keeps the crash log lines and backtrace in flash for `GET /gameday/crash`.
- `components/improv_ble`: a marked local override of ESPHome's
  `improv_ble` (changes between `GAMEDAY: begin` and `GAMEDAY: end`): it
  answers Improv's "which networks can you see" request, keeps the password
  out of the log and goes back to the saved network after a failed handover.

## From ESPN to the panel

1. **Deciding what to read.** `GamedayComponent::loop()` runs on ESPHome's
   main loop and never waits on the network. Depending on the mode (My team,
   Favorite teams, Live games) and the game phase, `start_job_` fills one
   `Job` describing the reads: the clock (an ESPN `Date` header when NTP is
   silent), a finals recap, a live-games scan, the next games, a soccer cup,
   the team's schedule, and the game itself.
2. **Reading.** A short-lived FreeRTOS task (`gameday_fetch`, 16 KB stack)
   runs the job. `open_` sends `User-Agent: curl/8.0` (ESPN's edge refuses
   unknown agents) and treats a 403 or 429 as throttling, reading
   `Retry-After`. `ContainerReader` streams the response through a 1 KB
   buffer into ArduinoJson with a per-endpoint filter, so a scoreboard of
   several hundred kilobytes is never held in RAM; documents live in PSRAM.
   URLs are built in `espn_parse.cpp` from `kLeagues` in `leagues.h`
   (`/teams/{id}`, `/teams/{id}/schedule`, `/scoreboard/{event}`,
   `/scoreboard?dates=`), under `site.api.espn.com`.
3. **Parsing.** `espn_parse_impl.h` turns each document into a
   `GameSnapshot` oriented around the followed team, with per-sport extras
   from the `sport_*.cpp` files (the count, outs and runners, soccer minute
   and cards, periods and series).
4. **Applying.** When the task sets `job_done_`, the loop calls
   `apply_job_`. A job from before a team or mode change (older
   `generation_`) is dropped; a task past its deadline is abandoned and the
   read retried.
5. **Timing.** `interval_for_phase_`: 10 s while live, 1 min in the last hour
   before the start and for the linger after the final (30 min, 10 for
   baseball), 15 min while the game is far off, and the team re-read every
   6 h. After a failure, `back_off_` waits 30 s doubling to 10 min, or 5 min
   doubling to 1 h after a 403 or 429, honoring `Retry-After`
   (`poll_timing.h`).
6. **Splashes.** `decide_splash` compares the new snapshot with the last one
   (per sport) to pick a touchdown, field goal, goal, home run or win splash.
7. **Drawing.** `emit_` builds `UpdateFields` (status, clock, scores,
   situation, ticker text and so on), rebuilds the state document, and
   `draw_` calls the `on_update` trigger. In YAML that publishes Game Status
   and Last Play for Home Assistant and runs `gd_update` in
   `pages/gameday-live.yaml`, which updates the LVGL labels. Logos come from
   `logo_cache.cpp` (48 decoded 32x32 logos, least recently used out), and a
   board with new logos waits up to 3.5 s for them.

The boot cache (`startup.h`) keeps the next game in flash for a day so a
restart can skip the team read.

## The device page and its routes

`firmware/web/app.js` and `app.css` are the page; `scripts/build_web.py`
prepends the team and timezone tables and writes `bundle.js` (and
`bundle-demo.js` with `demo.js`), which ESPHome's `web_server` embeds. CI
fails when a bundle is stale.

`GamedayComponent` is also an `AsyncWebHandler` for every `/gameday/` path:

| Route | What it does |
|---|---|
| `GET /gameday/state` | The state document (`rebuild_state_` on the main loop, under `state_mutex_`), plus fresh `health` and `wifi` objects |
| `POST /gameday/set?key=value` | Settings (`set_request.h`, keys in `kSetKeys`), applied on the main loop |
| `POST /gameday/action?do=` | `refresh`, `qc`, `power_ok` (and the demo's `demo`, `demo_stop`) |
| `GET/POST /gameday/wifi` | The saved networks; `forget=<ssid>` |
| `GET /gameday/crash`, `POST ?clear=1` | The stored crash report |
| `GET /gameday/heap` | Free memory, for the burn-in monitor |

The page reads the state document, re-reads it when ESPHome's event stream
sends a Game Status event, holds each tapped setting until the panel
confirms it, and uses entity endpoints only for what Home Assistant also
has (Brightness, Power, the update entity). In the browser it reads today's
games and logos from ESPN and release notes from GitHub.
`tests/fixtures/set_cases.json` is the shared table of settings cases that
the host tests, the page's mock and the fuzzer all use.

## Wi-Fi, Bluetooth and setup

Wi-Fi and Bluetooth share one radio. Bluetooth stays off at boot (it slowed
Wi-Fi from 5 to 12 s), comes on for setup or after a 30 s grace when a known
network doesn't answer, and goes off once Wi-Fi connects. A panel offline
for a minute after it had been connected restarts once
(`gd_offline_count`). After "Reset Wi-Fi" the panel starts in setup for
15 minutes (up to 25 while someone is connected), then goes back to its
saved networks. Setup works through the installer's Wi-Fi dialog
(`improv_serial`), the hotspot and captive portal, or Improv over Bluetooth.
`captive.h` notices a network that wants a sign-in page and says so on the
panel and the page.

## Brightness, heat and power

`guard.h`: the heat guard steps the brightness down to 70, 45 or 25 percent
of the setting as the chip warms (75 C and up), straight to 25 percent at
85 C, and back up below 70 C. Two brownout restarts in a row (with no ten-minute
run in between) cap brightness at 64 out of 255 until the owner clears it
with `power_ok` on the device page.

## Updates and channels

- **Release**: `${site_base}/firmware/<variant>/manifest.json`, published by
  a release tag's CI run. Checked every 6 hours. Nothing installs unless the
  owner turns on "Install updates automatically", and then only between 3 and
  5 AM, at the panel's own minute, never during a game it follows, one try a
  night (`auto_update.h`). A version that rolled back is not retried on its
  own.
- **Follow beta**: release firmware built from the `beta` branch, at
  `${site_base}/firmware/<variant>-beta/manifest.json`, chosen with the
  "Follow beta" switch (`beta_channel` in the component). Betas are named
  for a release still to come (`scripts/beta_version.sh`), and
  `version_newer` puts every beta before the release with its number, so
  turning the switch off gets a panel offered that release once it ships.
- **Bar builds**: `${site_base}/firmware/beta/<variant>/manifest.json`,
  checked every 15 minutes and installed on their own at night, or right away
  when the beta's summary says "Install now:" with the panel's name.
- **Mirror**: the component still supports an `update_mirror` second host,
  but no build configures one.

## Crash reports and health

`components/crash_report` keeps the last crash (log lines and backtrace)
across restarts, and leaves out a restart the firmware asked for (it reads
the planned flag in the health block's restart log, the firmware's only
RTC_NOINIT variable); `python3 scripts/crash_report.py <panel>` prints it and
decodes the backtrace with the release's ELF. `health.h` records why the
panel last started (planned, power on, crash, watchdog, brownout) in RTC
memory and reports it, with uptime, memory and the ESPN status, in the
state document.

## Sites

- `docs/site`: the installer and support site for GitHub Pages
  (`site_base`). Pages: home (the main installer, scoreboard75), `install/diy`
  (MoonHub75 one or two panels), `diy`, `setup`, `support`, `privacy`,
  `changelog`. Links are relative so the site works under the Pages project
  path. A release deploy adds `firmware/<variant>/` manifests and images, the
  beta channel folders, the rendered changelog, FAQ JSON-LD, `sitemap.xml`
  and `robots.txt`. `scripts/site_seo.py check docs/site` checks each page's
  tags.
- `docs/wiki`: the documentation (Astro and Starlight). Pages are markdown
  under `docs/wiki/src/content/docs/`; the changelog page is generated from
  `CHANGELOG.md` at build time. It has no published home yet.

## CI (`.github/workflows`)

- `build.yml`, on PRs, pushes to `dev`, `beta` and `main`, and `v*` tags:
  - Release notes (the CHANGELOG section must exist for a tag; the
    changelog page must render).
  - Branch rules: `beta` takes PRs only from `dev`, `main` only from `beta`;
    `main` and tags need a final ESPHome; no private notes files; tags must
    be on `main`.
  - Builds: the three release variants, the two demo and two bar builds, and
    an allowed-to-fail build on the next ESPHome beta.
  - Size budget against `firmware/budget.json` (flash, static internal RAM,
    and the parser's memory per ESPN read), with a growth limit per PR into
    `dev`.
  - Parser tests: `make -C tests`, `replay`, `heap`, the script selftests,
    the site check, the device page mock check and the fuzzers.
  - Device page tests: bundle freshness and Playwright.
  - On `beta`: publish the beta channels to the rolling `beta-firmware`
    prerelease, and ask `site.yml` on `main` to republish (skipped while
    there is no release).
  - On a tag: build the site, deploy it to Pages, create the release with
    the factory images and manifest zips.
- `site.yml`: republishes `docs/site` from `main` with the latest release's
  firmware, skipped while a release is pending or none exists.
- `docs.yml`: builds `docs/wiki` on PRs that touch it.
- `docs-check.yml`: a PR into `dev` that changes `firmware/` or
  `components/` must change a docs page or carry `docs: not needed`.

## Scripts

Generators (`build_web.py`, `build_teams.py`, `build_team_colors.py`,
`build_timezones.py`), release and site helpers (`release_notes.py`,
`site_seo.py`, `check_site_firmware.sh`, `add_beta_channel.sh`,
`beta_version.sh`, `set_manifest_name.sh`, `publish_mirror.sh`), the size
budget (`size_budget.py`), and bench and factory tools (`factory_flash.py`,
`burnin_monitor.py`, `improv_scan.py`, `crash_report.py`,
`bar_report_qr.sh`). The Python tools and `beta_version.sh` have
`--selftest`.

## Tests (`tests/`)

- `make -C tests`: `test_parse.cpp` (parsers, game logic, splash rules,
  timing, updates, favorites, recap and more against the ESPN documents in
  `tests/fixtures`), `golden_football.cpp`, `test_set.cpp` (the settings
  table) and `test_networks.cpp` (saved networks).
- `make -C tests replay`: a tool that replays recorded single-event
  documents through the parse, text and splash code.
- `make -C tests heap`: the memory each ESPN read takes, for the size budget.
- `make -C tests fuzz`: mutated ESPN documents, the Date and Retry-After
  headers, the sign-in probe's answer, the boot cache and settings requests,
  under the address and undefined behavior sanitizers, seeded from the
  fixtures.
- `tests/web`: Playwright tests of the device page against
  `tests/web/mock_device.py`, which reads the settings keys and limits from
  the C++ headers.
