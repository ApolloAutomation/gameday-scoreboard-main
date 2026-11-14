# Architecture

How Game Day Scoreboard is put together: the builds, the components, the
path a score takes from ESPN (or for minor league baseball, the MLB Stats
API, and for the AHL and the ECHL, HockeyTech's feed) to the panel and the
device page, the update channels, and the sites,
scripts, CI and tests around them. File and line
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
  `update_source`, `beta_update_source`, panel geometry, the AHL's and the
  ECHL's HockeyTech site keys and client codes (`hockeytech_*`), and
  `hub75_studio_ref` (the hub75-studio commit the theme and clock packages are
  pinned to).
- **esp32**: a 16 KB loop task stack; Bluetooth and mbedTLS buffers moved to
  PSRAM so TLS has internal RAM; Wi-Fi's code out of internal RAM; malloc()
  falling back to PSRAM when internal RAM has no piece big enough (and
  taking PSRAM first above 16 KB); a shorter SNTP start delay. Four panels'
  DMA buffers take 144 KB of internal RAM, which is what these make room
  for.
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
- **gameday**: the component's settings (`beta_channel`, `hockeytech`,
  `on_action`, `on_update`).
- **Home Assistant entities**: only the ones worth automating (Team, Mode,
  Power, Brightness, Scroll Speed, Select Page, Install Updates
  Automatically, the buttons, Game Status, Last Play, IP, ESPN Data, RSSI and
  disabled heap sensors). Everything else lives in the device page's own
  state document.

### Pages (`firmware/pages/`)

- `boot.yaml`: the boot and setup overlay on LVGL's top layer: starting,
  setup (QR code, hotspot name), joining, Wi-Fi lost, sign-in Wi-Fi, the
  address screen, and the boot button hold (10 s Reset Wi-Fi, 20 s factory
  reset, 30 s cancel). Off Wi-Fi the QR code is a Wi-Fi join code for the
  panel's open hotspot, shown only once the hotspot is up; on Wi-Fi it is the
  device page's `http://<ip>/` (`components/gameday/setup_qr.h`). `boot_view`
  sets it with `lv_qrcode_update`, only when the text changes.
- `gameday-live.yaml`: the scoreboard page's widgets, for boards 64, 128,
  192 and 256 pixels wide, with the splashes and fireworks (which splashes
  get them: `splash_fireworks`, `alerts.h`). `tt_bind_view` hands the
  widgets to the board renderer (`components/gameday/board_view.cpp`) once;
  `tt_apply_layout` lays them out with its `board_layout` and `on_update`
  draws each update with its `board_draw`. The page keeps the leaderboard's
  rows (`gd_board`), the clock's time, the ticker's scroll time and the
  fireworks component, which the renderer starts, stops and places through
  `fx_run`: clear of a splash's words (`splash_layout`'s `fx`), the points
  on black where it reaches them (`pts_mask`), and lent to the second
  area's splash while the board isn't using it. A splash stays up
  `kSplashMs` (`alerts.h`), on the board (`tt_hide_splash`) and in the second
  area. A 2x2 (128x128) has the 128 wide board in its top half and the
  second area (split mode, `components/gameday/split.h`) in its bottom half
  (`gd_lower`); three and four in a row can give their right end to it, the
  board then 128 wide on the left. `tt_apply_layout` takes both places from
  `split_area()` and runs again on the component's `layout` action, with no
  restart. The second area's clock is `gd_lower`'s time, date and bigger
  ticker, ending above the heat and power line; its game card is built in
  C++ under `gd_lower` (`area_build`) and drawn by the component itself
  (`area_draw`). The second area's splash covers that area only; the
  board's covers the board, and all four panels of a 2x2. The splash's logo
  tiles, its points and a Large clock's AM or PM are built in C++ too
  (`board_build`). Clock size and Text size (`Prefs8::sizes`) pick the fonts
  and places: `game_geom`, `clock_card_geom` and `lower_half` in
  `board_layout.h`; a size change runs `layout` again, which also sets the
  Clock page's fonts (`gd_clock_page`, `clock_page_fonts`).
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
  `espn_parse.cpp/.h`, `espn_parse_impl.h`, `box_score.cpp`, `sport_baseball.cpp`,
  `sport_basketball.cpp`, `sport_hockey.cpp`, `sport_soccer.cpp`,
  `sport_golf.cpp`, `sport_racing.cpp`, `alerts.h`, `watch.h`,
  `source_mlbstats.cpp/.h` (minor league baseball from the MLB Stats API),
  `source_hockeytech.cpp/.h` (the AHL and the ECHL from HockeyTech),
  `source_markets.cpp/.h` (stock and crypto quotes), `markets.h`, `markets_view.h`,
  `app_markets.cpp/.h` (the Stocks & Crypto display),
  `leagues.h`, `poll_timing.h`, `offseason.h`, `recap.h`, `favorites.h`, `follow.h`, `idle.h`,
  `bets.h`, `players.h`, `props.h`, `split.h`, `apps.h`, `app_list.cpp`, `f1.cpp/.h`, `f1_parse.cpp`, `circuits.h`, `app_f1.cpp/.h` (the F1 display),
  `flights.cpp/.h`, `flight_airlines.h`, `plane_art.cpp/.h`, `plane_photo.h` and `app_flights.cpp/.h` (the Flight Tracker),
  `weather.cpp/.h`, `weather_art.cpp`, `map_canvas.h` and `app_weather.cpp/.h` (Weather), `startup.h`, `panel_text.h`, `badge.h`, `board_layout.h`, `set_request.h`, `captive.h`,
  `hotspot.h`, `setup_qr.h`, `health.h`, `guard.h`, `auto_update.h`, `update_mirror.h`, `qc.h`,
  `wifi_history.h`, `logo_lru.h`, `logo_icon.h`, `logo_decode.h`. `logo_cache.cpp`
  downloads and decodes logos on its own task, never the main loop: some of
  HockeyTech's "50x50" logos are 500 px PNGs that take about 2 s to decode.
  `board_view.cpp/.h` is the board renderer: a `BoardView` holds a
  board's LVGL widgets, `board_layout` places them for the board's width
  (the positions are `game_geom` in `board_layout.h`, which the host tests
  check) and `board_draw` draws one `UpdateFields` into them, its text
  through `panel_text`; an `AreaView` is the second area's widgets, drawn by
  `area_draw`. `demo.cpp` is the scripted demo. `teams.h`, `team_colors.h` and
  `timezones_parsed.h` are generated by `scripts/build_teams.py`,
  `build_team_colors.py` and `build_timezones.py`.
- `components/panel_layout`: the saved panel layout (one to four 64x64
  wide, or a 2x2 and how its rows are mounted), written into the HUB75
  display's config before the driver starts. Changing it reboots. More than
  two panels keep Bluetooth off: their DMA buffers leave it too little
  internal RAM.
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
2. **Reading.** A short-lived FreeRTOS task (`gameday_fetch`, 16 KB stack
   in PSRAM, `psram_tasks.h`; the loop frees it once it parks) runs the job. `open_` sends `User-Agent: curl/8.0` (ESPN's edge refuses
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
   and cards, periods and series, golf's leaderboard). Its copy and move
   are defined once, in `espn_parse.cpp`, rather than inlined into every
   file that moves one.
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
   to pick a big play (`alerts.h`: `detect_big_play`, a pure function of the
   two reads, the alert switches and whether the other side's plays count)
   or the win. Football's turnovers come from ESPN's last play: its type,
   its id (so a play ESPN rewords isn't new) and the team it ended with
   (`GameSnapshot::play_type`, `play_id`, `play_side`); baseball keeps the
   type and the play's own runs (`play_points`) for grand slams. The alert
   switches are their own preference blob (`Prefs9`), and in Live games
   "Favorite teams only" switches them all off for a game without the team or
   a favorite. The splash text goes to the panel through
   `panel_text(text, true)`, which keeps its line break. A `Splash` also
   names its side by abbreviation (`abbr`, or `both` for the whole game's)
   and its points (`splash_points`); `game_fields_` turns that into the
   update's side of the board (a read of another game can have its sides the
   other way round), and `splash_layout` (`board_layout.h`) places the logo
   tiles, the points and the words for each board, the side's name left out
   of the words. A tile shows the cached logo, or the team's badge until it
   comes in (`splash_logos`), so a splash never waits for a download.
7. **Drawing.** `emit_` builds `UpdateFields` (status, clock, scores,
   situation, ticker text and so on), rebuilds the state document, and
   `draw_` calls the `on_update` trigger. In YAML that publishes Game Status
   and Last Play for Home Assistant and draws the board with `board_draw`
   (`board_view.cpp`) into the widgets of `pages/gameday-live.yaml`, then
   runs the page scripts its result asks for. Logos come from
   `logo_cache.cpp` (128 decoded 32x32 logos, least recently used out), and a
   board with new logos waits up to 3.5 s for them. A logo a card on screen
   or held back needs downloads before a next card's head start
   (`logo_job_before`); a prefetch from the fetch task that finds the cache
   full goes to the main loop, which makes room (`want`). The memo in
   `logo_lru.h` remembers per logo URL that its dark variant is missing, so
   the plain one is asked for first, or that it has no logo at all, which
   then isn't asked for again for 6 hours. A side whose logo isn't in shows
   its badge in the logo's place (`board_logos`). Another display's small
   logos (an `AppIcon`: Stocks & Crypto's 7x7 row logos, the Flight
   Tracker's 16x16 airline logos) are the same cache's entries, shrunk to
   a square at decode by `logo_icon.h`, a dark one lifted. The logo task
   does all of that decoding, every logo and icon, resting a tick every
   10 ms and refusing a source over a megapixel (`logo_decode.h`); the
   main loop only hears from `loop()` that a logo became ready or failed,
   and nothing on the task touches LVGL. An icon names
   its sources through a URL function; `app_fields_` asks for the first not
   failed, one download at a time, pins the shown ones (`pin_icons`) and
   redraws a display that waited on one once it loads or fails, so a
   failed source goes straight on to the next. A `mark` icon (the
   airline logos) also has a white background joined to the edge cleared
   and is dropped when too thin or dim to read, which then counts as
   missing, as does Google's gray stand-in; its `alt` label, the airline's
   code, shows until it loads.

The boot cache (`startup.h`) keeps the next game in flash for a day so a
restart can skip the team read.

**Bets.** The owner types bets in on the device page (`bet=`, `betdel=`;
the page sends slot 0, which `bet_put` turns into the first empty slot);
the panel never reads a sportsbook. `bets.h` holds up to eight `Bet`
records in their own blob (`ManualBets`, a `BetSource`; a bet tracker's
API could be another source). `bets_tick_`, run from `emit_`, after a
favorite's poll and every 10 s, hands each game the panel holds (the
board's, and in Favorite teams each favorite's card) to `bets_observe` as a
`BetGame`: a bet ties to its team's next or current game (a later game read
first gives way to an earlier one until it starts), keeps the final's
result and goes at local midnight (`bets_expire`). Live standing is worked
out from the snapshot each time and never saved. `emit_` adds the bets on
the board's game to the ticker, and the state document lists every bet.

**Split mode.** `split.h` is the second area's rules: where it goes
(`split_area`), what it shows now (`area_view`: the owner's choice, the night
hours, the main board's clock card, its team's game on the main board), the
scores rotation's card for it half a loop from the main board's
(`scores_step`), its team's poll interval (`a2_poll_ms`) and the rotation of
watched games (`rot_pick`, `rot_card`, `rot_ticker`). The setting is its own
16-byte blob (`SplitPrefs`, `gameday_split_v1`) and the `split` and
`splitteam` set keys. `emit_a2_` builds the area's `UpdateFields` and draws
them, keeping the card before up while a card with new logos waits up to
3.5 s for them (`LogoHold`), after which the held card's turn starts over;
the next card with logos (`scores_next_logos`, or the rotation's next
`rot_pick`) downloads its logos while the card before it is up, and the
rotation puts that card up next even when the one before it ended
(`rot_pick_next`). A live view with no card picks one at once rather than
drawing "No other live games", and the area turned off is cleared
(`area_clear`), so no stale card waits out a hold. On a 2x2 the board's
splash covers the area (`area_covered`): the rotation, the scores and a big
play's stay wait under it, and `a2_uncovered_` then starts the card's turn
over and gives an area splash it hid its whole time. `a2_tick_`
turns the rotation, ends a big play's stay and starts or
stops the scores reads for the area. Its reads go in the board's gaps
(`start_gap_job_`, in order: the area's own team poll or a big play's game
(`start_a2_job_`), the scores (`start_side_job_`, one league a read), then
the watcher), each only when it fits before the board's next read
(`WatchTimes`, cycled every 45 s whether the watcher reads or not); a due
area read that doesn't fit holds the others back. Area reads carry `Job::a2`
and their own generation (`a2_gen_`), failures (`a2_fail_`) and a deadline
the team read's look-ahead stops at. The gap reads that go to ESPN wait out
ESPN's backoff and, after ESPN refuses one, a hold (`espn_gap_ok_`); a minor
league second team's reads go to MLB or HockeyTech like the board's
(`fetch_schedule_`, `fetch_game_`) and keep to that host's wait instead
(`host_wait_`), a refusal there backing off that host alone (`a2_failed_`).
With Rotating live games, the watcher reads every live game of the sources'
leagues (`WatchCtx::all`), ESPN's only, as ever (`from_espn`), and a big play
jumps the area (`a2_jump_`) instead of taking the main board.

**Other displays.** Each area shows one display: Game Day (everything
above) or another, an `App` (`apps.h`) that `register_apps` in
`app_list.cpp` puts in its `Display` slot at boot; the firmware lists the
ones it has (`AppSlots::mask`) in the state document's `displays`, and the
page offers only those. The main area's choice is `display` (the `display`
set key, `SplitPrefs::display`, refused for one the firmware lacks); the
second area's are `SplitShow` 5 to 8, Game Day (the board's own game, the
area's `AreaView::GAME`, while the main area shows another) and one per
display (`AreaView::APP`, never the one the main area shows). `main_view`
puts the clock up in the night hours unless the display says it's busy,
and `app_main_dim` is Off or dim for it. A display reads in the board's
gaps (`start_app_job_`, after the area's own reads) only while it's on
screen, at its `poll_ms`, each read fitted with `AppTimes`, on its own
`Source` and `HostWait`; `open_` sends its method, body and headers
(`AppRequest`) and tells its hosts apart (`url_source`). On the fetch task
the body goes through Game Day's own JSON reader (`ContainerReader`), so a
display adds no second reader to flash: either it gives a `filter` and the
component reads the body as one document for its `parse_doc`, or its
`parse` streams an `AppStream`, looking ahead with `peek` and reading each
value with `json`. Once per read, before `apply` on the main loop, `ended`
hands it an `AppResult`: its host's HTTP status (0 when that host wasn't
read), whether it refused the panel and its Retry-After. A failed read
backs off the display's `HostWait`, unless `failed` asks for another host
now (the wait ends at once) or the display backs off each of its hosts
itself (`own_backoff`: the failure is timed and logged, the shared wait
left alone). Each read then rebuilds the state document
(`rebuild_state_`). `frame` fills an `AppFrame`: the leaderboard's title,
six rows (at the display's own columns and colors if it gives them,
`BoardLines::cols`, `pos_color` and `detail_color`) and ticker, one
polyline, and up to 40 labels the display places itself (`AppText`).
`app_fields_` turns it into the board's leaderboard fields: `gd_board`
(laid out by `rows_put`, which lays out golf's and racing's leaderboards
too), a `line` widget and a pool of labels made on first use (`put_texts`)
on the board, `RowsView`, its line and its own pool in the second area
(`area_build`). `app_env` gives displays the panel's local time
(`app_local`), timezone and firmware version, and `tz_miles` and
`tz_fahrenheit` the units it reads in: miles, mph and feet in the US and UK
time zones, Fahrenheit in the US ones alone. Its own set keys go after `kSetKeys`, its
settings in their own blob, its state under its key, its host's status in
the health block. Game Day's own reads, splashes and Home Assistant's
sensors carry on whatever the areas show; its splash shows only where Game
Day is on screen.

**The F1 display.** `F1App` (`app_f1.h`) reads ESPN's F1 header (the running
order, and the weekend's sessions as a backup schedule), Jolpica's next race,
standings and last result, ESPN's circuit facts and lap record, and the
weather at the circuit from Open-Meteo, one read per `request` by priority
(`next_read`), each kind due from its last good read (`f1_poll_ms`,
`f1_header_poll_ms`, 30 s on track). Each host (`F1Host`) backs off on its
own; Jolpica or Open-Meteo failing three times or refusing the panel moves
to its backup (the header's schedule, or ESPN's calendar after the race,
with the standings kept and aged; MET Norway's forecast) until a try after
30 minutes, doubling to 6 hours, answers. `frame` shows the order alone on
track, else rotates the cards (`rotation`, 10 s each, the weather 8 s) by
the clock, laid into the leaderboard's columns at 64, 128 and wider with one
ticker for the whole rotation, and redraws only when its card or countdown
changes. The data and frame text come from `f1.h`; the circuit outlines
are `circuits.h`, generated by `scripts/build_circuits.py` from F1DB.

**Stocks and crypto.** `markets.h` holds the owner's up to 8
symbols (`MarketBlob`, 100 bytes: a stock, an index or a coin each, kept
packed, with the slot whose sparkline shows) and their checks
(`parse_market_symbol` guesses the kind: `BTC` is a coin, `SPX` the S&P
500, `BRK/B` the class B share), the read URLs, the `MarketQuote` each read
fills (the price, the last close or the UTC day's open, the session's times,
64 sparkline buckets of a byte each), the market's state from those times
(pre-market, open, after hours, closed, `FRI CLOSE` after a weekend) and the
price, change and percent text cut to fit. Every source is keyless: one
Yahoo `v7/finance/spark` read carries every symbol and its sparkline; Kraken's
`Ticker` (the coins it lists) and Nasdaq's `quote/watchlist` (US stocks,
ETFs and two indexes) fill in when it fails. Both Yahoo and Nasdaq want a
browser's User-Agent (`kMarketsUserAgent`). `source_markets.cpp` parses each
with a filter into the slots it names. `markets_view.h` lays out the rows
(`market_geom`: symbol and price or percent on one panel, price and percent
on two, change too on three and four), the 64x64 one-symbol card
(`market_card`), the sparkline's points (`market_spark`) and the ticker with
its colored moves (`market_ticker`, the bets' green and red). `app_markets.h`
makes it the `Display::MARKETS` app: its list is the app's prefs blob, edited
by `mkt`, `mktdel` and `mktmove` (`markets_set`, which starts from the
defaults SPY, AAPL, BTC and ETH while nothing is saved), its quotes follow
their symbols when the list changes, and `MarketFeed` picks each read's host:
Yahoo until two failures or a refusal, then Kraken and Nasdaq back to back
(`App::failed` ends the shared wait for the switch) with Yahoo retried after
10 or 30 minutes, doubling to an hour. `market_merge` keeps the day's
sparkline under a backup's price. `markets_frame` lays the rows into the
leaderboard's labels at its own columns (`BoardLines::cols`, with place and
detail colors; `rows_cells` and `rows_put` in `board_view.cpp` place them on
the board and in the second area), turns the sparkline to the next charted
symbol and its page every 8 s and flips one panel's value between price and
percent every 4 s. `busy` is a listed market's regular session.

**The Flight Tracker.** `FlightsApp` (`app_flights.h`) reads Flightradar24's
feed for a box around home, falling back to adsb.fi and adsb.lol
(`feed_pick`), or the owner's receiver first, and looks up routes from
adsb.im or adsbdb; each host backs off on its own (`own_backoff`). The
plane reads, route checks and frames are `flights.h`, the airline names
`flight_airlines.h`, generated by `scripts/build_airlines.py`. Its
`--logos` run stores, per airline, which logo source reads best at 16
pixels (Google Flights or FlightAware, or neither) and the airline's color, so
the panel makes one logo download per airline and colors the plane's tail.
The card's airline is Flightradar24's airline field (`Plane::brand`, the
paint on the plane), else the callsign's. `plane_art.cpp` holds the side
views, ASCII art packed to 2 bits a pixel at compile time, and maps an
ICAO type code, else the ADS-B category, to one of them; `put_art` in
`board_view.cpp` paints the frame's `AppArt` into a PSRAM buffer. On a card
128 wide or more the card's plane also gets a photo: a read of
Planespotters' API (`Read::PHOTO`, by hex, else registration, with a
User-Agent naming the project) through the fetch task like the others, one
a plane, kept in `PhotoCache` (a day, a miss 12 hours) behind its own
`HostWait`. The frame's `AppPhoto` names the JPEG; `app_fields_` asks the
logo cache for it (`LogoCache::photo`, an entry of its own keyed by URL
and box, as logos are by URL and size), and once it loads it takes the
picture's place and its credit goes into the ticker after the card's
plane. Under 192 wide the list frame carries the photo without a box, so
the credit stays in the ticker while the card and the list take turns and
the ticker's text doesn't change every screen. The logo cache keeps at
most eight photos (`MAX_PHOTOS`), Weather's radar frames among them: a new
one replaces the oldest photo that is off screen and not downloading
(`pick_photo_evict_in` in `logo_lru.h`), before any logo, so photos hold at
most eight of its 128 places.
Planespotters' JPEGs are progressive, so the logo task decodes their first
scan alone with JPEGDEC, at 1/8, keeps the whole width and at least 60% of
the height, trimmed mostly from the bottom, scales that into the box and
dims what matches each row's ends (`plane_photo.h`), resting like the PNG
decoder.

**Weather.** `WeatherApp` (`app_weather.h`) reads the forecast from
Open-Meteo, failing over to MET Norway (`F1Host`, as F1's weather does),
the air quality from Open-Meteo's own service, the National Weather
Service's alerts for a US home, and RainViewer's list of radar frames; each
host backs off on its own. It keeps no home of its own: `App::home` lets
the Flight Tracker lend its saved one (`app_home`), and every read and
datum carries the home it was for, so a new home reads at once and an old
answer is dropped. Its parsers, URLs, words and map math are `weather.h`,
its icons `weather_art.cpp`, 2-bit pictures packed at compile time as the
planes are; a frame may now place up to eight (`kAppArt`), each in a PSRAM
buffer of its own size. The radar is a frame's `AppPhoto` naming a PNG:
`LogoCache::decode_map_` decodes it on the logo task with the logo
decoder's pngle into `MapCanvas` (`map_canvas.h`), the middle cut to the
box's shape and averaged over black with faint colors raised, so RainViewer's
256-pixel images centered on home and Iowa State's NEXRAD mosaic (asked
for at the box's own size and extent, the US backup) both fit any area.
`AppPhoto::hold` names the newest frame to draw while an older one loads,
`alt` a label shown until one draws, and `keep_art` leaves the frame's
pictures over it; the photo now sits under the frame's labels, line and
pictures, so the home's dot (`AppFill`) and ring (`AppLine`) draw over the
map. Radar frames step every 500 ms on `app_env().ms`, the component's
`millis()`. With its switch on, `WeatherApp` also checks alerts while off
screen (`App::idle_poll_ms` and `night_poll_ms`), and `App::ticker_warning`
hands warnings and watches to `draw_`, which puts them ahead of the main
board's ticker (`board_ticker_lead`) unless that ticker already says them. A
display's read now always counts as that display's in `open_`, so two
displays can share a host, as F1 and Weather share Open-Meteo.

**Big plays in other games.** The watcher (`watch.h`, "Switch to big plays
in other games", `ALERT9_NOWATCH` in `Prefs9`) reads other live games in
the board's gaps: `loop()` calls `start_gap_job_` while the next board poll
is not due, which runs the second area's reads and the scores read
(`start_side_job_`) before `start_watch_job_`, each fitted to the board's
next read and bounded (a scan per league every half hour or at the next
start, a read per source or game every cycle), so none keeps the others
from their reads (in Favorite teams, which ticks every second, the board's
next read is the locked game's next poll or the next team read,
`fav_gap_due_` and `watch_fav_due`). It starts a read (a `Job` with
`need_watch`) only when that read is further off than the read's last time
plus a second (`WatchTimes`, timed per league's day scoreboard and for a game
on its own by its last good read), so the board's 10 s poll waits only when
ESPN is slower than last time. A failed read that took longer than expected
(a hang to the HTTP timeout) keeps the time and holds its kind for the rest
of the cycle, then 3 and 7 cycles when it does it again, until one answers.
`watch_next` skips a read that doesn't fit for the next one that does, takes
the one tried longest ago first (then the sources' order and `watch_rank`'s),
so the reads a full cycle left out go first the next, and a kind no gap took
all cycle is expected an eighth quicker the next, so a slow scoreboard is
timed again instead of never read. Every 45 s (`kWatchCycleMs`) each source is
read once: in Live games a pro league's day scoreboard (`scan_url`), and one
game at a time (`event_url`) for a college league, whose games come from the
board's own scan (`watch_feed_`), and for the favorites in My team and
Favorite teams, found by their league's or college conference's day
scoreboard at their next start (the board's own card's start counts too,
`watch_want_day`), or when a game that's on leaves the board, which reads
its league's again (`watch_board_moved`; Favorite teams' playlist of cards
that aren't on reads nothing). Only sports with a big play switched on are read
(`watch_sport_mask`). Up to 8 games are kept (`WatchGame`, 72 bytes each, in
internal RAM; the documents are PSRAM's), favorites and close games first
(`watch_rank`, `watch_keep`, which never lets a list grow past 8). The fetch
task runs `detect_big_play` on each game against its last good read
(`watch_detect`, which rebuilds the read before from the `WatchGame` and
tells a home run from the score and the bases when the last play has moved
on), never on a game's first read or one more than two cycles old. A failed
read waits for the next cycle without holding up the others, a game whose
own read fails twice drops out (`watch_failed`), and a refusal (403, 429)
holds the watcher at least 5 minutes and backs the board off as its own
would (`watch_refused_`). Like the scores' reads, it reads nothing while the
board backs off, and nothing for a full retry after the loop gives up on a
worker; a watcher read given up on counts as a failed one that took the
whole wait (`WatchTimes::took`). A big play the board may take (`watch_may_switch`:
not during its own splash, not from the panel's team's game from its start
time on, `watch_live` (a golf tour's or a racing series' leaderboard while a
round or a session is on, `watch_own_game`), unless a favorite plays and "Switch from my team" is
on, a chained one only during the first stay, and `switch_allowed_()`,
false while Follow this game holds) starts a visit: `start_job_` polls that
game, its first poll carries the splash, and after "Stay on a big play", or
once the panel's team's game starts, `end_visit_` puts the mode's own board
back and reads it at once, that first read splashing only a final reached
meanwhile (`board_prev_`, `watch_back_prev`: the victory splash, never a big
play from while the board was away). Neither the mode's game nor the visit's
is watched. Home Assistant keeps the mode's game (`ha_game_`), and the state
document says `visit`, and `visit_follow` while Follow this game could still
pin the mode's game (`follow_can_start`): the page shows the switch only
then. Follow this game pins the mode's own game, the visit's saved
board (`watch_mode_game`), and ends a visit up then (`set_follow`, and
`watch_visit_over` with `allowed` false); while it holds, `watching_()` is
false, so nothing is read and the watched games start over after it. A
visit is busy for the idle display (`idle_busy_`'s hold): it shows over the
clock, the scores or Off or dim, at the night brightness in the night hours,
and the scores keep their scan's timing when back within two stays
(`side_off_ms_`). For bets, `bets_tick_` hands a visit's game to
`bets_update`, not `bets_observe`, and a watcher read hands back the games
the bets are tied to already (`WatchJob::bet_events`, `WatchJob::bets`):
those bets keep their details and take a result, but no read ties a bet to
a game or moves one.

**Favorite players.** The owner picks up to 16 players on the device
page, from a team roster the browser reads from ESPN (`player=`,
`playerdel=`); the panel never reads rosters. `players.h` keeps them in
`PlayerBlob`, saved as two 8-slot halves (`PlayerHalf`): slots 1-8 under
v2026.11.3's key (`gameday_players_v1`), so an update keeps them, and slots
9-16 under `gameday_players2_v1`; a change saves only the half it touched
(`player_half_bit`). A job takes a `WatchRoster` at its start
(`watch_roster`: the favorites' ids and leagues, then the soccer prop
players'), and a game read (`fetch_game_`) passes `parse_event` a
`PlayerWatch` of the roster's ids in that game's league (`roster_watch`,
each id once). With none, the
filter and the parse are as before; with any, the read also keeps the
last play's id, points and athletes, ESPN's leaders (football's for the
game, basketball's per team, baseball's both) for the favorites only, and
soccer goals' scorers (`fill_player_filter`, `players_from_event`).
`emit_` adds `player_lines` (a favorite's leader line while the board's
game is live) to the ticker before the bets, and the state document has
them as `game.pl`. `board_splash_` goes through
`board_splash_with_players`: with the Favorite player alert bit on,
`player_scored` credits a favorite on the play that scored between two
live reads (by id in football, baseball and basketball, never on a
football try, whose athletes are the touchdown's, and in basketball only
for a late lead change or a buzzer beater; by goal in soccer; by an
unambiguous name in the goal's words in hockey) and its splash takes the
team's place. The page folds a player's name with the firmware's own
`ascii_fold` table (`build_web.py` copies it into the bundle), so a hockey
name matches the goal's words. During a visit (watch.h) the board's game is
the visit's: its poll reads with the visit's league's favorites, the
stat lines take `board_league_()` (the visit's), and `apply_visit_job_`
splashes through `visit_splash`, the watcher's splash on the first poll
and `board_splash_with_players` after it, so the play that brought the
board there splashes once and never with a name. The watcher's own reads
pass no `PlayerWatch`, so a favorite player never starts a visit, and
`watching_()` counts neither the final's bit nor the Favorite player bit
as a big play switched on. `watch_back_prev` leaves a favorite's score
from while the board was away unsplashed, as it does the team's.

**Player props.** The owner types props in on the device page (`prop=`,
`propdel=`, slot 0 the first empty), picking the player from a roster the
browser reads, or from the favorite players: over or under a half-point
line on one stat, or a yes prop. `props.h` holds up to 16 `Prop`
records in `PropBlob`, saved as two 8-slot halves (`PropHalf`) the way the
favorite players are (`gameday_props_v1` for slots 1-8,
`gameday_props2_v1` for 9-16, `prop_half_bit`), and the stats
(`kPropStats`: each a sum of up to three box score columns, its "atoms",
which `build_web.py` copies into the bundle). A prop ties to its player's
team's game the way a bet does (`bet_tie`, shared with `bet_observe`), on
the same games (`props_tick_`, run from `bets_tick_`). Its numbers come from
the game's summary (`summary_url`), read in the board's gaps
(`start_box_job_`, before the scores and the watcher): once a minute while
the game is live and once 30 s after the final, never while it doesn't fit
before the board's next read. The summary opens with its box score (25 KB
for basketball, about 190 KB for baseball, out of 250 to 950 KB), so a
`BoxCutter` on the `ContainerReader` ends the input there, closing the root,
and `parse_box` keeps the lines of the league's prop players only
(`box_score.cpp`, `BoxLine`, by ESPN's stat keys). The live number, the
pace (`game_progress`, `prop_status`) and the ticker words
(`prop_ticker`) are worked out each time; the final's read grades the
props (`prop_grade`, VOID for a player the box score never lists) and the
result stays until midnight. Soccer reads no box score: its anytime goal
counts the game read's goals, whose scorers' ids the read keeps when a
soccer prop is in the league (the job's `WatchRoster`). The state document lists
every prop with its number (`v`, half units) and status.

**Golf.** A golf tour (`League::PGA`, `League::LPGA`, `Sport::GOLF`) is a
team row in `teams.h` with ESPN's league id, but none of the team machinery
above applies. `start_job_` sends it to `run_board_job_`, which reads the
tour's scoreboard header (`header_url`, `apis/v2/scoreboard/header`,
about 19 KB during an event and 3 KB between them): the current or next
event, its status and round, and the top 25 in order with place, score to
par, holes played and tee time (`parse_golf`, `sport_golf.cpp`). After a
final, a canceled event or none listed, it also reads the season calendar
from the scoreboard of the coming Tuesday (8 to 12 KB, never `dates=` the
current year, which is 29 MB), or next season's (`golf_calendar_url`,
`parse_golf_calendar`); an event that started less than a day ago still
counts as ahead, since the header can list it hours after the calendar's
start. When that read fails and no next event is known, the final stays
up (`Golf::next_unknown`) and the next read waits on the failure backoff.
`golf_read_is_older` turns away up to two CDN copies in a row that go back
a round, a phase or an event (one that started two or more days before the
one shown: the calendar's card and the header put the same day three hours
apart). Timing is
`golf_poll_ms`: a minute while a round is played, 5 minutes between rounds
and before the start, 30 minutes otherwise, 6 hours between seasons. The
board is `gd_board_container` in `pages/gameday-live.yaml`, filled by the
`gd_board` script from columns `golf_lines` has already cut to fit
(`BoardLines`), read from the update being drawn (`board_fields`). The
board job, its reads and the drawing are shared code for any leaderboard
sport: the `board_*` functions in `espn_parse.cpp` pick the sport's own. `sport_golf.cpp` builds for
size (`#pragma GCC optimize("Os")`), since it runs once a minute at most.
Golf is not in Favorite teams or Live games (`set_request.h`).

**Racing.** A racing series (`League::F1`, `League::NASCAR`,
`Sport::RACING`) is followed the same way, through the same board job:
`is_leaderboard` sends it to `run_board_job_`, and the `board_*` functions
pick racing's parse, text and timing (`sport_racing.cpp`). The series'
scoreboard header (`header_url`, `sport=racing`) has one entry per session
of a race weekend, all with the weekend's event id, and each session's order
(up to about 95 KB for a finished F1 weekend, 3 to 27 KB for NASCAR, which
lists only its race). `parse_racing` picks the weekend (the one on track,
else the one whose last session with results ran latest, else the next) and
within it the session on the board: the one on track, else the latest with
results (`Racing::phase`: PRE, LIVE, BREAK between sessions, FINAL once the
race itself is over, OFF for a canceled race). Practice and qualifying gaps
come from the lap times, since ESPN's `behindTime` is wrong in a sprint
shootout. The calendar is the coming Tuesday's scoreboard, as golf's
(`parse_racing_calendar`); its entries' ids are in their `$ref`, and their
times run three hours late, which the parse takes off. A final stays a day
(`racing_show_next`), the clock shows when the next race is over 45 days
away (`kRaceSeasonGap`, `racing_off_season`), and `racing_poll_ms` reads
every 30 seconds during a race or sprint, a minute in practice and
qualifying, 2 minutes while a session is suspended or delayed, closer to the
next session as it nears, and every 6 hours while the off-season clock is up.
A held session still holds back an automatic update (`racing_on_track`)
until it has been stopped six hours past its start. After a final, the
calendar's next race also starts no earlier than the finished one
(`after_start`), so a NASCAR day's first Duel isn't next once the second is
over. The board is `gd_board_container` and `gd_board`, the same widgets as
golf's: on one panel `BoardLines::value_narrow` false hides the value column
and gives the names its room. Racing is not in Favorite teams or Live games
either.

**Minor league baseball.** `League::MILB` is the one league not read from
ESPN: its `LeagueInfo::source` is `Source::MLB_STATS`, and its 120 teams
(Triple-A to Single-A, `Team::group` the level's MLB Stats sport id, 11 to
14) come from the MLB Stats API's team list (`build_teams.py --league milb`).
The four fetch functions hand a minor league team or league to the MLB Stats
reads in `source_mlbstats.cpp`, all on its schedule endpoint with a
`fields=` list that cuts each document on the server and an ArduinoJson
filter as well: the team read (`mlb_team_url`, yesterday to ten days ahead
at all four levels, about 4 KB, which also gives the Up next list, so no
look-ahead or game-on-now read, and today's last final for My team today
after a restart), the far read when that lists nothing
(`mlb_far_url`, 11 to 240 days ahead at the team's level, ids and dates only,
about 10 KB: next season's opener), the game poll (`mlb_game_url`, one game
with its line score and probable pitchers, about 1.4 KB; the line score
endpoint has no status or team ids) and the day scan for Live games and the
finals rotation (`mlb_day_url`, up to about 25 KB for 60 games, of which the
first `kRecapCap` finals are kept). Each turns into the same `Schedule`,
`Upcoming`, `GameSnapshot` (sport `BASEBALL`, so the baseball board, ticker
and splashes draw it), `LiveGame`, `Final` and `Pending` as ESPN's.
`codedGameState` decides the state: postponed, canceled and suspended games
are post and not completed, as ESPN has them, and a game the API lists on
several dates (postponed, then played) is read at the entry nearest the
kickoff the board picked. A game with no start time (game 2 of a
doubleheader) sits at midnight Eastern like ESPN's TBD games, and
`pre_poll_near` reads a baseball one every minute on its day, and
`day_order` puts it after game 1 for My team today. Logos are
MLB's 64 px PNG spots (`mlb_logo_url`). There are no team colors, last play
or home run flag in the feed, so its big-play splashes (`alerts.h`) are runs
and walk-offs, the latter from the 7th in a 7-inning game
(`Baseball::innings`), and it has no rank, seed or place.

**Minor league hockey.** `League::AHL` and `League::ECHL` (the last two
values the 16-bit league masks have room for) are read from HockeyTech's
feed (`Source::HOCKEYTECH`, `source_hockeytech.cpp`), the leagues' own stats
service, with each league's public site key and client code from the
`hockeytech_*` substitutions (the component's `hockeytech:` block, into
`ht_set_feed`). Their 62 teams come from its `teamsbyseason` view
(`build_teams.py --league ahl --league echl`); `Team::group` is the season
their logo file is named for, plus `kHtNoSmallLogo` when the logo server's
50 px copy is missing or too large for the logo cache, and `emit_` then
draws the team's badge (`badge.h`) in the logo's place. The four fetch
functions hand an AHL or ECHL team or league to three reads: the team read
(`ht_team_url`, the `scorebar` view for one team, whose day counts are
games around the league's today: one back and five ahead, about 11 KB, with
both records, the Up next list and today's last final for My team today
after a restart), the game poll (`ht_game_url`, the game
centre's `clock` tab, about 1.9 KB, which the feed's server caches a minute
where the scorebar's is four; `ht_fill_from_schedule` adds the records and
a TBD placeholder it lacks) and the day scan (`ht_day_url`, the
`gamesbydate` view, about 6.5 KB a finished game with its goals, up to
about 105 KB; its start times are local, turned to UTC by
`ht_local_epoch`'s North American zones). Status codes 1, 2, 3 and 4, 8 and
9 are not started, in progress, final (3 unofficial), postponed and
suspended; the clock at zero in a period, or the words, mark an
intermission, and a shootout leaves the period at OT, so the words and the
scoring's `SO` key mark it, and the goal count takes the deciding goal at
the end. There are no team colors, plays, power plays, ranks, seeds or
places in the feed, so a goal from the score is its one big-play splash
(`alerts.h`) and the Standing setting leaves its teams their record. The
feed refuses requests that carry another site's Origin, so the device
page's On now leaves both leagues out.

Each host other than ESPN keeps its own accounting. `open_` tells the
hosts apart (`is_mlb_url`, `is_ht_url`): a read's status, refusals and
`Retry-After` go to the job's `host[]` slot for its `Source`, and a job
that read only one other host backs off on that host's `host_wait_`
(`HostWait`, poll_timing.h), never ESPN's run, and the other way round.
Favorite Teams skips a favorite whose host is waiting (`host_waiting_`), a
scan leaves a waiting host's leagues out while others are left, and a
failed read from one host in a scan that read others backs off that host
alone (`host_scan_result`). The idle display's scores reads keep to the
same waits: they leave a waiting host's leagues out (`drop_source`), and
their own failed read from one counts in its wait (`host_side_result`).
Their bytes count in the same "ESPN Data" figure, and the health block adds
`mlb` and `mlb_ago`, `ht` and `ht_ago`. The
big-play watcher and favorite players stay ESPN's (`from_espn`):
`watch_sources` and `watch_wants_teams` leave the other hosts' leagues out,
`parse_player` and `player_valid` refuse their teams, Favorite Teams'
watcher gap counts a favorite whose host waits as due when that wait ends
(`watch_after_wait`), and ESPN refusing a watcher read holds My team's
board only when its team is ESPN's (`watch_refusal_holds_board`).

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
games and logos from ESPN (minor league baseball's from the MLB Stats API
and MLB's image server, AHL and ECHL logos from HockeyTech's logo server, but
not their games) and release notes from GitHub. The cards under the mode bar
are `<details>` elements; which ones a viewer opened is kept in that
browser's localStorage (`gd.cards`) and nowhere else, and `gd.expand` set to
`1` opens them all, which the Playwright tests do.
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
The web server gives a request to the first handler that takes it, and the
captive portal adds its handler only when the hotspot first starts, after
the device page's on a panel that already had a network. So the gameday
component adds one ahead of all of them (`hotspot.h`): while the portal is
up and the panel is off Wi-Fi, every GET but `/gameday/` gets the setup
page, the phones' internet checks included. Its loop also has the portal
answer up to eight DNS lookups per pass of the main loop instead of one.
`captive.h` notices a network that wants a sign-in page and says so on the
panel and the page.

After a new join with "Show how to connect" on, the IP address and `.local`
name lead the ticker for 30 s once the boot screen goes (`setup_qr.h`,
`show_join_address`), and a live game on a panel that has a team cuts the
join's QR screen short (`board_live`, `boot.yaml`). The scoreboard page
hides the ticker under a full-board message, and lends it to the message
while the line is on (`join_line`, `ticker_host`), so the address still
shows under "Can't reach ESPN" or "Getting ready".

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
memory and reports it, with uptime, memory and the ESPN, MLB and HockeyTech
status, in the
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
  table), `test_alerts.cpp` (every big-play alert, and that each splash fits
  the panel's lines on one to four panels), `test_bets.cpp` (manual bets),
  `test_props.cpp` (player props and box score reads),
  `test_watch.cpp` (the big-play watcher: which games, big plays from day
  scoreboards, when a read may run, when the board switches and goes back),
  `test_players.cpp` (favorite players), `test_apps.cpp` (the display
  picker, its frame helpers and a stand-in display), `test_f1.cpp` (the F1
  data core), `test_app_f1.cpp` (the F1 display's reads, failover and
  screens), `test_markets.cpp` (stock and crypto symbols, quotes, text and
  frames), `test_markets_app.cpp` (the Stocks & Crypto display's list,
  failover, frames and state), `test_flights.cpp` (the Flight Tracker: its
  feeds, sources and failover, routes, photo lookups, screens, settings and
  the display's reads), `test_weather.cpp` (Weather: each source, failover,
  units, icons, the radar's map math, pages and frames at every size), `test_plane_art.cpp` (plane pictures and kinds, airline logos
  and the logo steps), `test_logo_decode.cpp` (the logo task's PNG
  decoding: size checks, fit, painting and rests), `test_plane_photo.cpp`
  (the photo's cut, scaling and lighting) and `test_networks.cpp`
  (saved networks).
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
