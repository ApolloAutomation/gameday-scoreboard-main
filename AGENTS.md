# Notes for coding agents

ESPHome firmware for a standalone sports scoreboard on an ESP32-S3
driving HUB75 LED panels: the NFL, college football, MLB, minor league
baseball, the NBA, the WNBA, men's college basketball, the NHL, the AHL, the ECHL, MLS
and the Premier League, and golf's PGA Tour and LPGA Tour and racing's Formula 1 and
NASCAR Cup Series, each followed like a team. It
follows games by itself, from ESPN's public site API, for minor league
baseball MLB's public stats API (statsapi.mlb.com), and for the AHL and the
ECHL HockeyTech's feed (lscluster.hockeytech.com), and serves its own
settings page. Apollo Automation maintains this fork of it at
github.com/ApolloAutomation/gameday-scoreboard-main. CONTRIBUTING.md has
the same ground for people; this file is the short version.

## Layout

- `firmware/gameday.yaml` (MoonHub75 pinout) and
  `firmware/gameday-scoreboard75.yaml` (the scoreboard75 controller board)
  are the two release builds. Each only names itself and picks a pin map from
  `firmware/controllers/`; everything else is in
  `firmware/gameday-common.yaml`. `firmware/gameday-2panel.yaml` is
  gameday.yaml booting two panels wide, offered by both installers.
  `firmware/gameday-demo.yaml` and `firmware/gameday-scoreboard75-demo.yaml`
  add the scripted demo (`firmware/pages/demo.yaml`) for test builds; they
  are never released.
- `firmware/gameday-bar.yaml` and `firmware/gameday-scoreboard75-bar.yaml`
  are the bar builds, for the free panels in bars: the same firmware plus
  `firmware/pages/bar.yaml`, which reads the beta channel
  (`firmware/beta/<variant>/` on the project's site, the `site_base`
  substitution in `firmware/gameday-common.yaml`) and installs from it on
  its own at night. CI publishes them only from `beta`. Release
  firmware never installs anything unless the owner turns on "Install
  updates automatically".
- `components/gameday`: fetching, parsing and game logic (C++): ESPN for
  every league but minor league baseball, which `source_mlbstats.*` reads
  from the MLB Stats API, and the AHL and the ECHL, which
  `source_hockeytech.*` reads from HockeyTech's feed. HockeyTech wants each
  league's public site key and client code, the `hockeytech_*`
  substitutions in `firmware/gameday-common.yaml` (a data feed's keys, not
  a lock on the panel).
- `components/panel_layout`: the panel layout (one to four panels wide, or
  a 2x2 and its wiring), applied at boot from a saved preference.
- `components/saved_networks`: the up-to-five Wi-Fi networks the panel
  remembers (ESPHome keeps one), handed to the Wi-Fi component at boot.
- `firmware/pages`: the LVGL pages drawn on the panel.
- `firmware/web`: the device page. `bundle.js` is generated from `app.js`
  and the team and timezone tables, and `bundle-demo.js` is the same plus
  `demo.js` (both by `scripts/build_web.py`, with comments and indentation
  stripped, so edit `app.js`); `app.css` is served as is.
- `docs/site`: the installer and support site. Every page needs its own
  title, description, canonical link and share tags;
  `python3 scripts/site_seo.py check docs/site` lists what's missing.
  `docs/site/diy` is the page for DIY owners (boards, panels, wiring);
  keep it in step with `firmware/controllers/` and the README's Hardware
  section.
- `docs/wiki`: the documentation site (Astro + Starlight). CI builds it
  on every PR that touches it; it has no published home of its own yet, so
  links point at the markdown in this repo. Pages are markdown under
  `docs/wiki/src/content/docs/`; `docs/wiki/README.md` has the
  conventions.
- `scripts`: generators (`build_web.py`, `build_teams.py`,
  `build_team_colors.py`, `build_timezones.py`, `build_circuits.py`,
  `build_airlines.py`, `build_airports.py`, `build_sprites.py`,
  `build_apollo.py`), release
  notes and factory tools.
- `tests`: host tests for the parser and game logic, and Playwright tests
  for the device page in `tests/web`.

## Build and test

- ESPHome 2026.10.2, the version CI pins (`ESPHOME_VERSION` in
  `.github/workflows/build.yml`). Use `esphome compile
  firmware/gameday.yaml` (and the scoreboard75 file) to check a change;
  `esphome config` misses C++ errors.
- Host tests: `make -C tests` (needs g++). CI also builds
  `make -C tests replay` and runs the `--selftest` of
  `scripts/size_budget.py`, `factory_flash.py`, `burnin_monitor.py`,
  `improv_scan.py`, `crash_report.py`, `site_seo.py`, `release_notes.py`,
  `build_airlines.py`, `build_airports.py`, `build_sprites.py`,
  `build_apollo.py`, `check_docs_links.py`,
  `scripts/beta_version.sh`, `set_manifest_name.sh`,
  `keep_previous_ota.sh`, `check_live_feed.sh` and
  `tests/web/mock_device.py`. The last checks the device page's mock
  against `tests/fixtures/set_cases.json`, the same table the host tests
  run through `components/gameday/set_request.h`.
- CI also runs `make -C tests sanitize`: the unit checks built with the
  address and undefined behavior sanitizers. Apple clang's ASan won't
  start, so on a Mac `make -C tests sanitize SANITIZE=undefined` runs
  them under UBSan alone.
- A panel that restarted from a crash, a watchdog or a brownout keeps the
  report in flash (`components/crash_report`, `GET /gameday/crash`).
  `python3 scripts/crash_report.py <panel>` prints it and decodes the
  backtrace with the release's ELF.
- Fuzzing: `make -C tests fuzz` runs mutated ESPN documents and set
  requests through the parsers under the address and undefined behavior
  sanitizers for about a minute, as CI does on every PR;
  `make -C tests fuzz-long` runs 30 minutes a target. clang with libFuzzer
  makes it coverage guided; other compilers get a random mutator. A crash
  leaves its input in `tests/build/crash-*`, and libFuzzer's timeouts,
  memory overruns and leaks in `timeout-*`, `oom-*` and `leak-*`; pass that
  file to `tests/build/fuzz_espn` (or `fuzz_set`) to replay it, and add a
  host test with the fix.
- Size budget: CI's "Size budget" job checks each build's flash (the OTA
  image) and static internal RAM (from the ELF), and the memory each ESPN
  read takes on the host (`make -C tests heap`, `tests/heap_budget.cpp`),
  against `firmware/budget.json`. A PR fails when a figure goes over its
  budget, or (into `dev`) grows more than the file's `max_growth` against
  dev's last build; the job summary has the table. A change that needs
  more raises that one entry in the same PR and says why. Growth past
  `max_growth` takes the maintainer's `size-growth-ok` label on the PR and
  a re-run of the Size budget job, not an edit to `max_growth`.
  `python3 scripts/size_budget.py seed <sizes folder>` prints figures with
  10% headroom (at least 1 KB).
- Device page tests: in `tests/web`, `npm ci` and
  `npx playwright install chromium` once, then `npm test`.
- After editing `firmware/web/app.js`, `app.css` or `demo.js`, run
  `python3 scripts/build_web.py` and commit `bundle.js` and `bundle-demo.js`.
  CI fails if either is stale.
- `components/gameday/teams.h`, `team_colors.h`, `timezones_parsed.h`,
  `circuits.h`, `flight_airlines.h`, `flight_airports.h` and `sprites.h`
  are generated by `build_teams.py`, `build_team_colors.py`,
  `build_timezones.py`, `build_circuits.py`, `build_airlines.py`,
  `build_airports.py` and `build_sprites.py`. Rerun the script rather than
  editing them by hand
  (`build_teams.py --league milb` reads the minor league teams from the MLB
  Stats API, `--league ahl --league echl` the hockey teams from HockeyTech).
  The play animations' art is ASCII in `firmware/art/sprites/*.txt`: edit
  it there and rerun `build_sprites.py`. CI's `build_sprites.py --check`
  fails a stale `sprites.h`. `apollo_logo.h`, Apollo on the start-up
  screen, is `build_apollo.py`'s scaling of `firmware/art/apollo/apollo.png`
  (Apollo Automation's logo); CI's `build_apollo.py --check` fails a stale
  one. The Flight Tracker's airport names are `firmware/data/airports.tsv`
  (from OurAirports), packed into `flight_airports.h`; fix a name in
  `build_airports.py`'s tables and rerun it, and CI's
  `build_airports.py --check` fails a stale header.

## Rules

- Branch from `dev` and open PRs against `dev`. `beta` and `main` only take
  promotion PRs, and CI enforces that.
- A PR description says what changed and how it was tested. No links to
  private repos or trackers and no ticket numbers from them. Maintainers
  keep reviews and progress notes privately.
- Commit messages are one plain sentence about the behavior that changed
  ("Name each panel's hotspot after its suffix"), not "fix bug" or a
  conventional-commit prefix.
- Never commit `secrets.yaml`, Wi-Fi details, session notes, plans or
  specs. This repo is public; CI rejects common notes files.
  `ARCHITECTURE.md` and `REVIEW_REPORT.md` at the root are public
  documents and are fine.
- The panel trusts the home network on purpose: OTA has no password, the
  ESPHome API has no key and the device page is open. Apollo Automation
  decides that posture; don't add passwords or keys to the default build
  in a feature PR. The README section "On your network" explains why.
- Team logos come from ESPN (minor league baseball teams' from MLB, AHL and
  ECHL teams' from HockeyTech) at runtime, and airline logos from Google
  Flights or FlightAware. Don't commit league, team or airline logos.
- Version bumps and CHANGELOG sections are written by maintainers at
  release time. Leave both alone in a feature PR.
- A change an owner or DIY builder can see or do comes with its docs
  page, updated or added under `docs/wiki/src/content/docs/` in the same
  PR. CI's "Docs updated" job fails a PR into `dev` that touches
  `firmware/` or `components/` without one, unless the PR carries the
  label `docs: not needed`. Describe behavior `main` doesn't have yet in a
  `:::note[Coming in the next release]` aside. `npm run build` in
  `docs/wiki` must pass.

## Code comments

- Keep every comment 3–10 words max.
- Never mention conversation history, chats, or decisions made with the user.
- Never state whether something was or wasn't tested.
