# Game Day Scoreboard

## Credits and thanks

- **Brandon Harvey ([@bharvey88](https://github.com/bharvey88))** built Game Day Scoreboard for Apollo Automation. Thank you, Brandon, for all the work that went into it, and for releasing it under the MIT license, so everyone can use, change and share it (see [LICENSE](LICENSE)).
- Apollo Automation maintains the project.
- It is built on [hub75-studio](https://github.com/pavlov-net/hub75-studio)'s LVGL pages and controller packages, and on the attribute model of the [ha-teamtracker](https://github.com/vasqued2/ha-teamtracker) integration. Thanks to both projects.

![Touchdown fireworks on the panel](docs/site/media/touchdown.gif)

A standalone scoreboard for HUB75 LED matrix panels. Install it from your browser, join it to WiFi, pick your team on the panel's own web page, and it follows the game from ESPN by itself. Nothing else to run. It covers the NFL, college football, MLB, the NBA, the WNBA, men's college basketball, the NHL, MLS and the Premier League.

Watch the [full demo](https://apolloautomation.github.io/gameday-scoreboard-main/) on the installer page. The [docs](docs/wiki/src/content/docs/) cover setup, every setting, each sport's board, troubleshooting and running the firmware on your own board.

If you already use Home Assistant and want room lighting to celebrate too, [gameday-matrix](https://github.com/bharvey88/gameday-matrix) does the same thing through a blueprint.

## What it does

- Next-game card while you wait: both logos, season records, kickoff day and time in your timezone, and a ticker with the betting line, over/under, TV network and venue
- Live scoreboard at kickoff: scores, the game clock and quarter in large type under them, down and distance beneath that (red in the red zone), abbreviations in team colors with the team holding the ball in gold, records under the logos, timeout pips, and a scrolling ticker with the down and distance, where the ball is and the last play (each has its own switch; the clock keeps its own row and never goes in the ticker)
- Full-screen splashes in your team's color for touchdowns, field goals, extra points and two-point conversions, the opponent's scores acknowledged in theirs, and a victory splash on a win
- Each sport has its own board: the inning, count, outs and a small diamond of the runners in baseball; the minute, stoppage time, latest goal and red cards in soccer; the period and clock in hockey and basketball, with the playoff series when there is one. Home runs and goals get the fireworks too
- Lingers on the final (half an hour, ten minutes for baseball), then looks up the next game
- Live games: follows a random game in progress in the leagues you choose and moves on when it ends
- A device page that mirrors the panel, celebrates with it, lists the next three games in football and soccer, and holds every setting
- Up to 16 favorite teams from any league, cycling through the ones playing today, with the first four on a WizMote remote's numbered buttons
- Updates itself over WiFi from the device page when a new release is out, or on its own between 3 and 5 AM if you turn on Install updates automatically
- Adapts to one 64x64 panel (logos side by side, stacked scores) or one 128x64 panel or two 64x64 side by side (logos at the edges, big scores, records and pips in the middle); one firmware, the count is a setting

## Hardware

Building your own? [Runs on your own panel](https://apolloautomation.github.io/gameday-scoreboard-main/diy/) lists the boards and panels it runs on, how to wire a bare ESP32-S3, and what differs from a Game Day Scoreboard.

- The [Apollo Automation M-1 LED Matrix](https://apolloautomation.com/products/m-1-led-matrix) (Rev6), or any ESP32-S3 HUB75 controller on the [MoonHub75](https://github.com/MoonModules/Hardware/tree/main/MOONHUB75) pinout, with 16MB flash, octal PSRAM, and a button on GPIO0 (build `gameday.yaml`). The M-1 Rev6 uses the MoonHub75 pinout, microphone and button pins; the M-1 Rev4 uses other pins and needs its own build
- The scoreboard75 controller board (build `gameday-scoreboard75.yaml`)
- One 64x64 HUB75 panel, or a 128x64 display: one 128x64 panel or two 64x64 side by side (one firmware; pick the size on the device page)

The MoonHub75 pinout, which the M-1 Rev6 shares, under the names ESPHome uses (that board's README calls the upper half R0/G0/B0):

| R1 | G1 | B1 | R2 | G2 | B2 | A | B | C | D | E | LAT | OE | CLK |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 5 | 6 | 7 | 13 | 9 | 16 | 48 | 47 | 21 | 38 | 8 | 4 | 18 |

## Install

1. Open the [installer page](https://apolloautomation.github.io/gameday-scoreboard-main/), plug the controller in over USB, and click Install. That page is for the scoreboard75 controller board, which starts on a 128x64 display. Have an Apollo M-1 (Rev6) or another controller on the MoonHub75 pinout? Use the [DIY installer](https://apolloautomation.github.io/gameday-scoreboard-main/install/diy/) instead: it asks whether yours drives one 64x64 panel or a 128x64 display before you click Install, so the first boot uses the right layout.
2. Enter your WiFi details in the dialog that follows the install. If you skip it, the panel starts a hotspot named Game Day plus six letters and numbers, like Game Day 2f6a70, and shows the name on screen; join it and a setup page opens.
3. Once it's on WiFi the panel shows the `gameday-xxxxxx.local` name of its own page. Open that name on your phone or computer and pick your team there, or add the panel to Home Assistant (below). Until you pick a team or a mode, from that page or Home Assistant, the ticker asks you to and gives the panel's IP address for phones that can't resolve `.local` names. Holding the controller's boot button for 1.5 seconds shows the name and the IP address again any time.

The project's site at that address (the installers and demo, the DIY, support and changelog pages) and the update feed go live with the first Apollo Automation release. Until then, build the firmware from YAML (see [Build it yourself](#build-it-yourself)) and read the [docs](docs/wiki/src/content/docs/) in this repo.

While it waits for WiFi, and right after it joins, the panel also shows a QR code for the Game Day app. That app is a separate project that Apollo Automation does not maintain, and it may not find panels running this firmware; the hotspot and the device page do everything it does.

Team changes take effect immediately and survive reboots. Wrong panel size? Click the matching picture in the Setup card. Nothing needs reflashing to change teams or layouts.

## The device page

The device serves its own page: a live board that mirrors the panel (logos, score, clock, down and distance or the count and outs, timeout pips, last play), a team chooser with logos, search and a tab per league plus an "On now" tab listing today's games straight from ESPN (tap the side you want to follow), and the settings grouped in plain language. It is plain HTML and JavaScript embedded in the firmware, so it works with no internet beyond the logos and the "On now" tab, which your browser reads from ESPN. The page reads one JSON document from the device (`GET /gameday/state`) and writes settings back to `POST /gameday/set`, so it is not limited to what an ESPHome entity can carry.

| Setting | What it does |
| --- | --- |
| Show | My team, Favorite teams, or Live games: a game in progress picked at random from the leagues you choose, switching every few minutes |
| Team | All 32 NFL teams, every FBS college football team, every MLB, NBA, WNBA, NHL, MLS and Premier League team and every Division I men's college basketball team, a tab per league, plus an "On now" tab |
| Timezone | Follows the browser's zone unless you pick one by hand; used for kickoff times |
| Ticker: game situation, last play, odds and TV | Choose what scrolls along the bottom. Game situation is down and distance in football, the count and outs in baseball, and goals, cards and stats in soccer |
| Opponent scores too | Turn the opponent's scoring splashes off if you only want yours |
| Favorites | Up to 16 teams in priority order. "Only today's games" keeps the playlist to the favorites playing today. The first four are the numbered buttons on a WizMote remote |
| Remote | Pair a WizMote: turn discovery on, press any button on the remote. ON/OFF, brightness and NIGHT buttons control the panel |
| Panels | One 64x64 panel, or one 128x64 panel or two 64x64 side by side. Changing it restarts the device |
| Firmware / Check for updates | The device checks this project's releases every 6 hours; an Install button appears on the page when a newer version exists and updates over WiFi |
| Install updates automatically | Off unless you turn it on. Installs a new release between 3 and 5 AM, never during a game the panel follows |
| Brightness, Panel on, Ticker speed, Showing | Panel brightness, panel on or off, ticker speed, scoreboard or clock page |
| Refresh scores, Reboot | Re-fetch right away, restart |

## Home Assistant is optional

The device runs on its own, but it is a normal ESPHome device. If Home Assistant is on the same network it discovers the panel under **Settings → Devices & services**; press **Add** and there is no key to enter. Not discovered? Add the ESPHome integration and enter the panel's `gameday-xxxxxx.local` name or IP address:

[![Open your Home Assistant instance and start setting up a new integration.](https://my.home-assistant.io/badges/config_flow_start.svg)](https://my.home-assistant.io/redirect/config_flow_start/?domain=esphome)

It comes with the controls worth automating: Team, Mode, Power, Brightness, Scroll Speed, Select Page, Game Status, Last Play, Firmware, Install Updates Automatically, Refresh Now, Reboot and the WizMote pairing switches. Favorites, timezone, ticker options, splashes and the panel count are set from the device page only. Men's college basketball teams aren't in Home Assistant's Team list (it would be too long for an entity); pick them on the device page. The API has no encryption key in the prebuilt binary; build from YAML if you want one.

## Build it yourself

```
git clone https://github.com/ApolloAutomation/gameday-scoreboard-main
cd gameday-scoreboard-main/firmware
esphome run gameday.yaml
```

`gameday.yaml` only names the build and picks the controller pin map from `firmware/controllers/`; everything else lives in `gameday-common.yaml`. The panel count is a saved preference applied at boot by `components/panel_layout`, so one binary serves both layouts. The scoreboard page is in `pages/gameday-live.yaml`, the ESPN logic in `components/gameday`, and the device web page in `firmware/web` (edit `app.js` or `app.css`, then run `python3 scripts/build_web.py` to refresh the embedded bundle). The theme and clock packages come from [hub75-studio](https://github.com/pavlov-net/hub75-studio), pinned to a commit.

## How it works

The `gameday` component asks ESPN's public site API for the team's next event, then polls that game's own scoreboard entry, in every league: every 15 minutes while the game is far off, every minute inside the last hour, every 10 seconds during the game, showing ESPN's clock as it arrives, and every minute for half an hour after the final (ten minutes for baseball). After a failed read it waits longer before trying again. The JSON is streamed through a filter on the device so a scoreboard of several hundred kilobytes never has to fit in memory at once. Score deltas between polls decide the splashes, the same rules the blueprint uses. Logos are ESPN's dark-background PNGs (the plain one when a team has no dark version), decoded and shrunk to 32x32 on the ESP32.

The parser and game logic have host tests: `make -C tests` (needs g++). The device page has browser tests against a mock of the panel's web server: in `tests/web`, run `npm ci` and `npx playwright install chromium` once, then `npm test` (needs Node and Python 3). To refresh the team list at the start of a season, run `python3 scripts/build_teams.py` and commit the regenerated header.

## Notes

- ESPN's API is unofficial and can change without notice. The device sends a curl-style User-Agent because ESPN's edge rejects unfamiliar ones.
- All HTTPS requests (ESPN, logos, firmware updates) are certificate-verified against the ESP-IDF bundle. If ESPN ever moves to a certificate authority outside that bundle, fetches will fail until a rebuild.
- Team logos are ESPN's and are fetched at runtime, not bundled.
- Fetching runs on its own task, so the panel animation keeps going while a document downloads.

### On your network

The panel is a score display, and it treats your home network as trusted.
Anyone already on your Wi-Fi can open its page and change the team, and
updates are not password protected. That is a deliberate trade: it keeps
setup to one tap in the app and keeps the panel working when your phone
is the only thing that knows it exists. It is not designed to be exposed
to the internet, so do not forward a port to it.

## Contributing

Contributions are welcome: bug reports from game days, test fixtures from odd games, builds for new boards and fixes to the device page. [CONTRIBUTING.md](CONTRIBUTING.md#where-help-is-wanted) says where help is wanted and how to build and test. Docs changes go in `docs/wiki` alongside the code they describe.

## License

MIT.
