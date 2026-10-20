# Review report

Apollo Automation started maintaining this fork of Game Day Scoreboard in
October 2026 and then reviewed the whole repository. This report covers
both: the changes that set up the fork, every change the review made and
why, what it left alone on purpose, and what to do next. It describes `dev`
as of the review's merge.

## Summary

- The repository now lives at
  [ApolloAutomation/gameday-scoreboard-main](https://github.com/ApolloAutomation/gameday-scoreboard-main),
  with the `dev`, `beta` and `main` branches CI enforces. Every update
  channel, the site and the project's GitHub, docs and support links point at
  this repository and its GitHub Pages address,
  https://apolloautomation.github.io/gameday-scoreboard-main. The panel's
  setup QR code, the store links and the bar builds' report email still point
  at the earlier project's addresses (see "Decisions made during the
  review").
- The review made 114 commits across ten areas and two follow-up passes. 39
  are correctness fixes (two of them only make the host tests read settings
  the way the panel already did), from
  undefined behavior a fuzzer can reach (an over/under value, score deltas,
  soccer records) to a day count off by one across New Year, a cache that
  evicted the wrong logo after 49 days of uptime, crash reports that counted a
  reboot as a crash, and device page controls that keyboards and screen
  readers could not reach. The rest are tests, docs, CI fixes and small
  cleanups. Almost every behavior change comes with a test that proves it; the
  exceptions are a panel_layout config check, the crash report's planned
  restart fix (checked against the linker map) and three workflow fixes.
- Host checks grew from 8,850 to 10,534, the device page tests from 54 to
  69, and the settings table from 68 to 80 cases. All seven builds compile,
  and the three release builds the size budget covers stay well within it.
- Nothing about the panel's network posture changed. Many findings in
  protected areas (polling, Wi-Fi, updates) are written up below as
  recommendations, not changed.
- `main` stays at the import commit: it builds only on a final ESPHome
  release (unless a maintainer adds the `esphome-beta-ok` label to the
  promotion PR), and ESPHome 2026.10 has no final yet. The site and the update feed
  go live with the first Apollo Automation release.

## Repo health before and after

| Check | Import commit (before) | After the review |
|---|---|---|
| `esphome compile`, seven builds (ESPHome 2026.10.0b3) | All pass | All pass |
| Update hosts in the firmware images | Another project's site and mirror | Only the Pages address; no mirror |
| `make -C tests` | 8,850 checks, 0 failures | 10,534 checks, 0 failures |
| Saved networks host test | 130 checks | 130 checks |
| `make -C tests fuzz` (UBSan) | Passes by chance; the fuzzer can reach two undefined behaviors (over/under, soccer record) and score overflows | Clean over long runs |
| Script self-tests | 7 pass | 8 pass (release_notes.py has one now), with more cases |
| Device page tests (Playwright) | 54 pass | 69 pass |
| Settings table (`tests/fixtures/set_cases.json`) | 68 cases | 80 cases |
| Size budget | Within budget | Within budget (flash about +3 KB, static RAM slightly lower) |
| CI on `dev` | Green | Green |

Two local differences from CI, unchanged by this work: on macOS, Apple
clang's address sanitizer runtime did not start, so the fuzzers ran locally
under the undefined behavior sanitizer alone (CI runs both on Ubuntu); and
the local Node was older than the docs build needs, so CI's "Build docs" job
is the docs check.

## Setting up the fork

Merged into `dev` as
[#1](https://github.com/ApolloAutomation/gameday-scoreboard-main/pull/1).

- **Credits.** A "Credits and thanks" section at the top of the README
  credits Brandon Harvey, who started Game Day Scoreboard, and the hub75-studio
  and ha-teamtracker projects it builds on. LICENSE keeps its existing line
  and adds one for Apollo Automation.
- **Maintainers.** Code owners of the project's own components are
  `@ApolloAutomation`. CLAUDE.md and AGENTS.md name Apollo Automation as the
  maintainer and keep every working rule (branch model, docs with every change,
  size budget, no private notes). The ESPHome project name is
  `ApolloAutomation.gameday-scoreboard`.
- **Links.** GitHub links, the device page's source link and release-notes
  lookup, and the factory and crash-report scripts point at this repository.
  Site links point at the Pages address, docs links at the docs in this repo,
  and support at this repo's issues.
- **Update channels.** One substitution, `site_base` in
  `firmware/gameday-common.yaml`, builds every update URL: releases, Follow
  beta and the bar builds' channel. CONTRIBUTING.md's "Moving to a new
  address" lists every place the address lives, so a future domain is a
  short, known edit.
- **No second update host.** The release builds no longer read a mirror when
  a manifest read fails. The mirror code and its tests remain for a build that
  configures one.
- **Workflows.** The site deploy and the beta republish skip with a warning
  while the repository has no release, and fail on any other GitHub error.
  Nothing in CI was loosened.
- **Site.** Links are relative so the site works under the Pages project
  path; canonical and share tags use the Pages address. A launch-list signup
  form that posted visitors' emails to another server is gone, with its
  privacy section, and so is a "Show and tell" link to Discussions this repo
  doesn't have.
- **Boards.** `firmware/controllers/moonhub75.yaml` matches the Apollo
  Automation M-1 LED Matrix Rev6 board on every HUB75 pin, the microphone pins
  and the button, so the docs name the M-1 (Rev6) as a board for
  `gameday.yaml` (the DIY installer). The scoreboard75 controller board keeps
  the main installer.
- **Docs.** Setup leads with the installer's Wi-Fi dialog, the panel's
  hotspot, the device page and Home Assistant. The Home Assistant page covers
  discovery and adding the ESPHome integration, with My Home Assistant
  buttons.
- **Tried and backed out:** ESPHome Device Builder adoption
  (`dashboard_import`). The adopted configuration failed because
  `firmware/pages/guard.yaml` names its two images relative to the
  configuration folder, which is the adopter's folder for a remote package.
  Changing both to `../images/guard-heat.png` and `../images/guard-power.png`
  fixes it: a stub configuration with only the package then compiles, and the
  local builds still validate. That is the first item under "Further
  improvements".

## Audit changes

Each change below is one commit on `review/full-audit`, merged into `dev`
with this report (plus the commit that added ARCHITECTURE.md). The review
ran in three rounds: ten areas, with every commit checked by a second
reviewer before it was kept; a second pass on confirmed leftovers; and a
final review of the whole diff, whose ten confirmed findings were all
fixed. A few bullets are lightly edited from their commit message. The sentence is the commit message, which says what
changed for the owner or the builder; the area and the effect follow.

### Correctness fixes (39)

- Leave out an over/under so large it is no betting total, instead of casting it to int. (ESPN) Impact: An overUnder outside -10000 to 10000, or NaN, now reads as no over/under, so the ticker leaves out the O/U part.
- Drop the logo that has gone unused longest even after millis() wraps, not the one used just after the wrap. (ESPN) Impact: pick_evict now takes now and evicts the slot with the largest age (now - used).
- Keep the "no update" warning up through a long outage by stopping the miss count at 255 instead of wrapping it to 0. (ESPN) Impact: New espn::add_miss (poll_timing.h) stops the count at 255.
- Hand a finished fetch to the main loop through atomic flags, so the other core never reads the job before the worker has written it. (ESPN) No behavior change.
- Say Tomorrow and the weekday for kickoffs across New Year, which were a day off after December 31 of a 365-day year. (sports) Impact: Across New Year in a non-leap year the panel now says 'Tomorrow' and the weekday as on any other day.
- Leave a soccer record with a run of four or more digits as it came instead of overflowing an int while turning it into points. (sports) Impact: Each field is read with a 3-digit width (%3d), the same kind of width-limited sscanf guard parse_iso8601_z uses (%4d and %2d there).
- Make a hot panel wait its full five cool minutes before brightening a step even when the first cool reading lands at the millis() wrap. (pages) Impact: A cool reading at exactly millis() == 0 starts the five-minute cool wait like any other reading.
- Read a sign-in redirect to a relative link as proving nothing even when its query carries ESPN's address, and one that starts with // as pointing at another host. (Wi-Fi) Impact: A 3xx answer whose Location is relative now classifies UNKNOWN even when its query contains a URL.
- Install a bar beta right away when "Install now" names the panel in capitals, as the publish form allows, or says "All". (updates) Impact: Bar builds only: the names listed after "Install now:" and the word "all" now match in any case.
- Never install an update on its own when its version has a sign, a space or an empty part, which used to read as a newer number. (updates) Impact: New version_parts() reads up to 4 parts as plain digits.
- Keep a Reboot or an update that comes back as a watchdog reset out of the crash report, as the health block already does, so it no longer counts as a crash or saves an earlier crash's backtrace again. (crash report) Impact: a Reboot or update that comes back as the RTC watchdog saves nothing to the crash report. The RTC variable this commit first used was replaced in the final review by the health block's planned flag (see below).
- Save the time of the restart in the crash report even when the clock is only set later, instead of the time the clock came back. (crash report) Impact: setup() and loop() both save boot_time(now, uptime_s) = now - uptime when the clock is valid, else 0.
- Decode the crashed core's backtrace and the other core's separately in scripts/crash_report.py, so the two stacks no longer read as one call chain. (crash report) Impact: New address_groups() splits the addresses at 'Other core (N) backtrace:' (or 'other core (N):' in a foreign-build report).
- Let scripts/crash_report.py print the crash and build times on Windows, where strftime has no hour without its leading zero. (crash report) Impact: Same output on macOS and Linux ('2026-10-19 1:05 PM'), now built with portable f-string formatting.
- Read set request numbers past int's range as int's limit instead of leaving them to atoi, so the host tests see what the panel does with a huge mode, rotate or lock-on. (device page) No behavior change.
- Refuse a panels= value that doesn't fit a byte, so panels=257 no longer switches the panel to one panel and restarts it. (device page) Impact: parse_set refuses panels outside 1..255, so the request is logged and ignored.
- Give the team chooser's league tabs their own wrapping row, so every league and the Close button fit on the screen instead of running off its right edge. (device page) Impact: Close now comes right after the search box in DOM order.
- Make the Time line's choose manually and follow this browser buttons, so the keyboard and screen readers can reach them. (device page) Impact: It is now a <button class=lnk> styled like a link.
- Make the team chooser a labeled dialog that keeps Tab inside it and gives focus back to Change team or Add a favorite when it closes. (device page) Impact: The sheet now has role=dialog, aria-modal=true and an aria-label ('Choose a team' or 'Add a favorite').
- Flip the Install updates automatically and Follow beta switches as soon as they are tapped and keep them through a slow re-read, like the other switches. (device page) Impact: autoupd and beta taps are held until the panel confirms them.
- Name each favorite's WizMote button by its slot on the panel, so a favorite after an emptied slot no longer shows the wrong button. (device page) Impact: Each row's 'remote button N' comes from that team's slot in the state document's favs.
- Tell screen readers the demo build's Made-up teams control is a switch and whether it is on. (device page) Impact: It now has role=switch and aria-checked kept in step with its state.
- Put the firmware box back to what the panel offers when an install never restarts it, instead of saying Installing until the page is reloaded. (device page) Impact: When the page gives up waiting, it clears installing and redraws the firmware box from the Firmware entity.
- Keep the Live games league chips in place between redraws, so a chip keeps the keyboard focus through a tap and every re-read of the panel. (device page) Impact: The chips are built once, and a render only updates aria-pressed and the on class.
- Keep the Time line's button in place between redraws, so it keeps the keyboard focus when the page re-reads the panel. (device page) Impact: The bold name, the muted span and the button are made once, and a render only changes their text and onclick.
- Keep the keyboard on the favorites list after Move up, Move down or Remove, on the moved team's button or the row that took a removed one's place. (device page) Impact: renderFavs remembers the focused row's team and button title.
- Refuse a team id past 32 bits instead of letting a 64-bit host wrap it to another team, so the host tests read team= and favs= as the panel does. (device page) No behavior change.
- Show Favorite teams with no favorites as the panel runs it, following the team: its Up next games and No upcoming game for this team rather than No live games right now. (device page) Impact: liveMode now uses isLive(mode), so mode 4 with no favorites reads like My team.
- Say the panel is restarting with a new layout only once it took the change; a layout post that failed leaves the picker usable and the board as it was. (device page) Impact: The page awaits setGD.
- Say a firmware install is running only once the panel took the Install press; one that failed leaves the firmware box offering the update again. (device page) Impact: The page awaits post().
- End both saved update versions before the boot check reads or logs either one, so a mark full of flash garbage never reads past its field. (second pass) Impact: New update_mark_terminate(UpdateMark&) in auto_update.h puts a NUL at index kUpdateVersionLen - 1 in both fields.
- Work out football and baseball scoring splashes from a 64-bit score difference, so a document with scores at the ends of an int never overflows and a drop never wraps into a splash. (second pass) Impact: New detail::score_gain(before, after) in espn_parse.h.
- Leave the chip temperature out of the health keys when it is infinite as well as when it is missing, so the state document stays valid JSON. (second pass) Impact: guard_json_keys now uses std::isfinite, so a non-finite temperature leaves the temp key out, the same as before the first reading.
- Set a panel opened from a browser in Ojinaga to US Central and one in Ciudad Juarez to US Mountain, the time those cities keep. (device page) Impact: A device page opened in a browser set to America/Ojinaga now sets the panel to US Central (an hour off before); one set to America/Ciudad_Juarez now sets US Mountain instead of saying the zone is not in the list.
- Show the Release notes link for a beta only when the update's release address is an https one. (device page) Impact: For a non-release version, release_url is used only when it starts with https:// (case-insensitive); otherwise no Release notes link is shown.
- Say under Wi-Fi networks that a network set up through the panel's hotspot is added too. (device page) Impact: the Device card's hint names the hotspot first.
- Point a reset panel's owners to its Game Day hotspot to set it up again. (device page) Impact: the restart message after Reset Wi-Fi or Factory reset names only the hotspot.
- Take the crash report's planned restart from the health block's restart log, so an update no longer shifts that log in RTC memory and reads as a crash once (final review). Impact: the crash report keeps no RTC variable of its own; the restart log stays at the start of RTC memory, so an update from earlier firmware keeps its boot count, last fault and planned flag.
- Say after forgetting the connected network that it stays only until the connection drops, or that the panel keeps the last one (final review). Impact: the device page's toast matches what the firmware does; Diagnostics also stays hidden when the panel sends an empty health block.

### Configuration and validation (1)

- Refuse a panel_layout config whose default panel count is more than its max_cols, instead of saving that count again on every boot. (pages) Impact: `esphome config` and `esphome compile` now fail with "default_cols (3) can't be more than max_cols (2)" for such a config.

### Performance (2)

- Move each parsed game and schedule into place instead of copying it, so a poll allocates fewer strings in internal RAM. (ESPN) No behavior change.
- Stop the walk back for older finals six days back, the oldest day the rotation keeps, so it no longer downloads a seventh day only to drop it. (second pass) Impact: recap.h gets kRecapBackDays = kRecapMaxDays - 1 (6) and recap_day_kept(day, today), which is (int64) today - day <= kRecapBackDays.

### Code quality (10)

- Send ESPN and its logo server the curl User-Agent from one shared constant, pinned by a host test. (ESPN) No behavior change.
- Decide every sport's victory splash in one place, so the rule that only a completed game has a winner cannot drift between sports. (sports) No behavior change.
- Refuse to build when the panel text table of Latin Extended-A letters loses or gains an entry, which would shift every letter after it. (sports) No behavior change.
- Size the boot button countdown's line buffer for any count, so the build no longer warns that "Hold 10s to" could be cut short. (pages) No behavior change.
- Describe the release builds' install rule in the bar page's comment in plain words instead of a shorthand label. (updates) No behavior change.
- Size the health block's buffer from health.h and test that its longest output fits, so a new key can't quietly empty the device page's Diagnostics. (crash report) No behavior change.
- Stop the build when the mode names and the modes the device page can set fall out of step, since a set mode indexes the names. (device page) No behavior change.
- Drop references to issues outside this repository from the loop stack and setup screen comments, keeping their explanations. (second pass) No behavior change.
- Size the guard keys' buffer from guard.h, so its fit test follows the firmware's buffer (final review). No behavior change.
- Bound the finals rotation's walk back with a tested recap.h helper, so the test covers the days the panel actually reads (final review). No behavior change.

### CI and scripts (12)

- Keep a draft GitHub release from stopping the site deploy, which crashed rendering the changelog page on the draft's empty publish date. (CI and scripts) Impact: release_dates() skips entries with no real publish date (year-1 zero time or empty/null).
- Fail the size budget when a figure it caps is no longer measured, instead of passing it unchecked. (CI and scripts) Impact: Each budget metric with no measured figure is now an error ('budget.json has firmware.a.static_ram but nothing measured it').
- Read a site page's title without the title of an inline SVG icon, so the search tag check compares the real titles. (CI and scripts) Impact: <title> inside <svg> (any depth, self-closing svg handled) is ignored for the page title.
- Name the project's own site, not the old gamedayscoreboard.app address, in the update mirror's warning when no mirror is set up. (CI and scripts) Impact: The warning now names ${SITE_URL:-the site}; SITE_URL is set at workflow level in both build.yml and site.yml.
- Check size growth against the newest dev build that has every figure, instead of a cancelled one that uploaded only some and left the rest unchecked. (CI and scripts) Impact: Each candidate is downloaded into `try` and counted against this run's figure sets (ls sizes).
- Fail a site republish started by a beta publish when its deploy carries no beta channel, instead of reporting success while the bar panels lose their manifest. (CI and scripts) Impact: For from_beta runs only, a deploy with no bar beta manifest is now an ::error:: and the step fails.
- Warn when a site deploy leaves out the beta channel because GitHub failed to answer, instead of saying there is no beta yet. (CI and scripts) Impact: Only gh's 'release not found' prints the old message.
- Keep the input of a fuzz timeout, memory overrun or leak on the CI run, not only of a crash. (CI and scripts) Impact: The 'Keep the input a fuzz failure left' step (renamed from '...crash left') uploads crash-*, timeout-*, oom-* and leak-* (if-no-files-found: ignore, unchanged).
- Report a malformed network result from the panel as a FAIL in improv_scan.py instead of crashing on it. (CI and scripts) Impact: The checks moved into a pure verdict(networks) that returns one line per failure.
- Keep a burn-in reading when the panel's event stream goes quiet on Python 3.9, which raised socket.timeout and counted the panel as not answering. (CI and scripts) Impact: Catches (TimeoutError, socket.timeout).
- Report a recorded game whose first document does not parse as a failed parse in the replay tool, keeping the no team ids note for a document that parsed without them. (second pass) Impact: A first document that fails to deserialize now prints 'parse failed: <file> (<ArduinoJson error>)' and counts as one parse and one failure.
- Stop crash_report.py with a note to retry or pass --elf when GitHub's release lookup fails, instead of a traceback on a rate limit or no network. (second pass) Impact: a lookup that fails for any reason other than a missing release ends with \"Couldn't look up release v<version> on GitHub (<why>). Try again in a while, or pass the build's ELF with --elf.\"

### Tests and fuzzing (27)

- Give the logo cache tie test the current time now that eviction compares ages. (tests) No behavior change.
- Expect "Install now: ALL" to reach every bar panel, as the case-blind install list now reads it. (tests) No behavior change.
- Test every ESPN parser against documents with missing, empty and null parts in each sport. (ESPN) No behavior change.
- Test that every sport gives the victory splash only to a game seen going from live to a completed final with a winner. (sports) No behavior change.
- Check that a rolled-back mark with no readable version is dropped at boot instead of kept as a failed update. (updates) No behavior change.
- Check that the overnight install still runs once a night when daylight saving skips or repeats an hour and across New Year. (updates) No behavior change.
- Test on the host how the crash report turns the crash handler's log lines into its text, and pin the saved record's size so a change can't silently drop a panel's report. (crash report) No behavior change.
- Test the health block's restart reasons the S3 rarely gives, a fault that already had its time, and a clock set back before the fault. (crash report) No behavior change.
- Have the device page mock read only ASCII digits in set requests, as the firmware does, so a mode, zone or team id in another script's digits is no longer taken as a number. (device page) Impact: The mock's regexes use re.ASCII and timezone_index requires isascii().
- Reset the mock at the start of the TBD kickoff test, so it no longer depends on the test before it leaving an upcoming game behind. (device page) No behavior change.
- Fuzz the Date and Retry-After headers, the sign-in probe and the plain logo fallback alongside the ESPN parsers. (tests) No behavior change.
- Fuzz the boot cache with every team read, checking it loads back as the same game or not at all. (tests) No behavior change.
- Test that every backoff wait stays inside its jitter window for any run length and that Retry-After takes whole seconds only. (tests) No behavior change.
- Test how the finals rotation shares its cards between leagues, odd cards and short leagues included. (tests) No behavior change.
- Test the exact edges of the lock-on, release, rotate and zone limits, team references and favorites lists in the settings parser. (tests) No behavior change.
- Test every reset reason the panel can report, which ones count as a fault, and the restart keys the device page reads. (tests) No behavior change.
- Test every Wi-Fi disconnect reason the panel sorts into a cause, and the cause keys the device page reads. (tests) No behavior change.
- Test the boot cache with the longest event id it holds, a full id with no end from flash, an undated game and no clock. (tests) No behavior change.
- Test version numbers with a missing or fifth part, the Install now list without a space, and the last start minute of the night window. (tests) No behavior change.
- Test full brightness at each heat step, the power cap edge, a missed temperature reading during a cool-down, and that the guard keys fit their buffer. (tests) No behavior change.
- Test the off-season clock at the horizon to the second, a home game with no abbreviation of our own, the last minute of the day and the color lift edge. (tests) No behavior change.
- Give the settings fuzzer a known team in every league so mutated requests reach each league's teams. (tests) No behavior change.
- Report a recorded game with no team ids in the replay tool instead of crashing on it. (tests) Impact: replay prints '! no team ids: <file>' for such a game and goes on to the next.
- Test the logo badge text and ring thresholds to the step, so the badge colors cannot drift. (tests) No behavior change.
- Test that Favorite Teams moves on cleanly when the slot on screen or pinned is past the end of a shortened list. (tests) No behavior change.
- Test that logo cache ties drop the first slot and that the plain logo fallback only rewrites a dark logo. (tests) No behavior change.
- Read a team id with a plus sign in the device page mock as the firmware does, and pin both signs in the shared settings table (final review). No behavior change.

### Documentation (22)

- Say the favorites logic picks from up to 16 favorites, not four, and drop references to issues outside this repository from code comments. (sports) No behavior change.
- Say in the page comments that QC starts from the factory flag or the device page, that records stay up on a wide board during play, and that the button countdown starts at 5. (pages) No behavior change.
- Say in the saved networks notes that forgetting a network leaves only the new first network in the live list until the next restart. (Wi-Fi) No behavior change.
- Say in the Wi-Fi history notes that a drop the panel causes itself comes from moving to a stronger access point, not from forgetting a network. (Wi-Fi) No behavior change.
- Give the real hotspot and Bluetooth waits in the Reset Wi-Fi boot comment, and say nothing restores the one-second waits before the next restart. (Wi-Fi) No behavior change.
- Say on the Wi-Fi page that a forgotten network the panel is on stays connected only until that connection drops or the panel restarts. (Wi-Fi) No behavior change.
- Say on the updates page that Follow beta shows the latest beta and that automatic installs then install betas overnight. (updates) No behavior change.
- Describe the device page routes as the firmware takes them: modes 0 to 5, rotate from 1, the favs, today and live keys, and the power_ok action. (device page) No behavior change.
- Stop saying in the README that the game clock can be added to the ticker, which the firmware has not allowed since the toggle was removed. (docs) No behavior change.
- Say in the README that the panel polls the followed game's own scoreboard entry in every league, football included, as leagues.h has it. (docs) No behavior change.
- Tell owners on the support page that a team between seasons gets the clock in its colors, not "No upcoming game". (docs) No behavior change.
- Tell scoreboard75 owners on the installer page to pick the matching panel picture when the board looks wrong, since that build already starts two panels wide. (docs) No behavior change.
- Name the leagues on the installer page as the firmware has them: men's college basketball, MLS and the Premier League. (docs) No behavior change.
- Send DIY builders on the setup page to the DIY installer to reinstall, not the scoreboard75 installer. (docs) No behavior change.
- Say on Pick your team that a panel with no team shows live NFL and college football games, the leagues it scans until a team is picked. (docs) No behavior change.
- List the Microphone Enabled switch that Home Assistant shows for an Apollo M-1 or MoonHub75 board on the Home Assistant page. (docs) No behavior change.
- List the crash_report component with the firmware's other components on Build it yourself. (docs) No behavior change.
- List the rained-out MLB fixture in the fixtures README next to the other MLB documents. (tests) No behavior change.
- Tell contributors to run the repo scripts with python3 and to commit both device page bundles with their change. (second pass) Impact: Docs only.
- Say on the Wi-Fi page that a forgotten network is joined again when it was the only saved one, and goes back on the list once the panel connects. (second pass) Impact: Docs only.
- Send Apollo M-1 owners to the DIY installer to reinstall, describe the ticker switches and the demo bundle exactly, and show Follow beta as current (final review). Docs only.
- Describe the fuzzers' wider coverage and the crash report's planned restart check in ARCHITECTURE.md (final review). Docs only.

## Decisions made during the review

Where a choice would normally go to a maintainer, the review took the most
conservative reasonable option:

- **Protected areas were not changed without an unambiguous bug.**
  ESPN polling and backoff, brightness and power limits, memory settings,
  Wi-Fi and Bluetooth behavior, update rules, the device page contract and the
  crash report format. Every behavior change in these areas is a bug fix, and
  all but the crash report's planned-restart fix (checked against the linker
  map) come with a test (for example the over/under and score overflow fixes,
  the update mark fix, the heat guard's millis() edge). The finals rotation's
  walk back now stops a day sooner, so a walk that goes the full distance
  reads one ESPN page fewer per league; nothing reads more. Everything else is in
  "Flagged and left alone".
- **Two reviewed commits were dropped.** They added build-time rejections of
  configurations no build uses (Follow beta with the same stable and beta
  manifest, an update mirror on its own source's host, a demo build with an
  update entity, a bar build without one) with no automated test. A smaller
  panel_layout check of the same kind (a default panel count above the
  maximum) was kept. See
  "Attempted and reverted".
- **Merging the ten areas** produced one conflict (two new tests called after
  the same line; both kept) and two integration fixes: a test that called the
  logo eviction function in its old form, and a test that expected the old
  case-sensitive "Install now" list.
- **The final critique caught a regression the review itself introduced.**
  A crash report fix had added a second RTC memory variable, which the linker
  placed ahead of the health block's restart log; every panel's first start
  after updating would have lost that log (its boot count and last fault),
  and an update whose restart came back as the RTC watchdog, as some do on
  the ESP32-S3, would have counted as a watchdog crash. The fix reads the existing planned-restart flag instead,
  and the linker map now shows the restart log alone at the start of RTC
  memory again. The comment next to it says to keep it the only RTC
  variable.
- **The fork setup merged with a known, pre-existing fuzz finding.** A local
  fuzz run found the over/under overflow on the setup branch; it reproduced
  on the import commit, so it was not a regression of the setup, and
  the audit fixed it.
- **The Device Builder adoption link was removed** when its stub failed; the fix is verified and listed below.
- **The main installer still offers the scoreboard75 build**; the Apollo M-1
  uses the DIY installer. Which build the main installer offers is a product
  decision.
- **Support routes to GitHub issues**, since the project has no support
  email of its own. Two places still name the earlier project's email: the
  privacy page's store card and the bar builds' report button.
- **Store links, the panel's setup QR code and the bar builds' report email
  were left as they were**, since there is no replacement yet.
- **The docs build's npm advisory was not fixed.** It is one moderate advisory
  (postcss-selector-parser below 7.1.6, a build-time tool), and npm's only fix
  is a major downgrade of Starlight.
- **No dependency that changes the firmware was bumped**: the ESPHome pin,
  the hub75-studio commit, the pavlov-net components and the GitHub actions'
  major versions stay as they were.

## Flagged and left alone

These were found and deliberately not changed, because they touch a
protected area, need a product decision, or need a panel to verify.
Recommendations follow each.

### ESPN polling, fetching and memory

- **Live games scans after a refusal.** When one league answers 403 or 429
  during a Live games scan, the scan still reads the remaining leagues, and if
  another league returns a live game the refusal does not lengthen the
  backoff (`clear_backoff_` runs). Recommendation: stop the scan loop on the
  first refusal and let the backoff decide, as the look-ahead already does.
  This lowers ESPN traffic, but it changes polling, so it was left for a
  maintainer.
- **A fetch task per job.** Every job creates a FreeRTOS task with a 16 KB
  internal stack (every 10 s during a game), as does the logo task. On a
  fragmented heap the task creation can fail and the read waits a minute.
  Recommendation: one long-lived task woken by a notification, or a stack
  allocated once at setup. Measure with the burn-in monitor.
- **An abandoned worker.** A fetch task past its deadline keeps running and
  can still write into the shared job while the next job starts. The comments
  accept this race; a heap-allocated job per worker would remove it.
- **A game that stops answering.** In My team, a game poll that keeps failing
  while the team endpoint works re-reads the team only on the 6 hour schedule
  (for example when ESPN renumbers an event). Recommendation: after a few 4xx
  answers that are not refusals, re-read the team early.
- **Late West Coast college games in Live games.** Football's scan day
  switches at midnight Eastern, so a game still on after 1 AM EDT can drop out
  of a Live games rescan. Recommendation: record a late Saturday scoreboard
  to see how ESPN dates those games, then use the 9 hour shift the other
  leagues use (and the same in the finals rotation).
- **The state document lives in internal RAM** and is copied on every page
  read; with 16 favorites it is several KB. Recommendation: measure on a
  panel; reserve or move it to PSRAM if it matters.
- **Small items:** the clock job's URL ignores a bench build's site root;
  ids and scores are read with `atoi` (clamped in practice); the unused
  per-scoreboard parse path does not fill per-sport fields; dead favorite and
  timezone select plumbing in `gameday.h`.

### Game logic

- **A safety reads as "2-POINT"** and a football overtime final says "Final",
  not "Final/OT" like the other sports. Both match the docs today; changing
  them is a product decision.
- **Season series text.** Hockey and baseball show ESPN's series summary of
  any type, basketball only playoff series. Record a regular-season event late
  in a season series to see whether ESPN sends one.
- **Favorites' "today"** is computed by subtracting the local hour, so it can
  be an hour off on a daylight saving change day. A PRE game with no
  parseable date would lock the Favorites board; ESPN always sends one in the
  fixtures.
- **Home Assistant's Game Status** says "Halftime" only for football and
  basketball halftime text, not soccer's "HT" or intermissions. Changing a
  recorded entity's text is a product decision.

### Panel pages and guards

- **QC timeout race:** a button press in the last 3 seconds before QC's 2
  minute timeout can restore a stale power state. Wrap the restore in
  `qc_stop` in a check that QC is still running.
- **The guard icon and line can cover the setup QR code** on a 64 pixel
  panel. Hide them while the code shows; test a scan with a phone.
- **WizMote:** an invalid MAC typed into the pairing field is logged but still
  added. Skip it when it fails the check.
- **Situation text** goes to the panel without the Latin-1 folding the other
  text fields get. Pass it through `panel_text()` like the rest.
- **The boot button's zero sentinel** (a press at millis() == 0) has the same
  edge the heat guard had; cosmetic.

### Wi-Fi and Bluetooth

- **Short waits after Reset Wi-Fi last the whole boot.** The setup boot cuts
  the hotspot and Bluetooth waits to 1 second and nothing restores them, so a
  later drop in that boot brings up the hotspot at once. Restore them in
  `wifi: on_connect` and bench-test a router reboot.
- **A blank network saved from the hotspot page** empties the saved network
  list (it is read as a reset from older firmware). A product decision.
- **The sign-in check reads ESPN while ESPN throttles the panel.** Skip the
  failing recheck while the last failure was a refusal.
- **Two ESPHome components log the Wi-Fi password** (the captive portal at
  INFO, Improv serial at DEBUG). Setting their log levels to WARN and INFO in
  `logger:` would drop those lines; check setup still works.
- **Bluetooth turns off as Wi-Fi connects**, possibly before an Improv client
  reads the final answer. Bench-check a phone setup.
- **Forgetting the network in use** leaves the Improv fallback pointing at it
  until the next connect. Refresh it in the forget path.

### Updates

- **Follow beta can briefly show the other channel's offer** when a check is
  already in flight as the switch flips. Treat the entity as unavailable until
  a read started after the switch answers.
- **Rollback detection can blame a later release** because the invalid mark
  in otadata stays until the next successful install into that slot. Record
  at install time whether an invalid partition already existed.
- **Beta versions sorted above their release.** Settled in the first
  release's pull request: a release now sorts after every beta with its
  number (`version_newer` and the device page's `isNewer`), so a panel that
  turns Follow beta off is offered the release its beta led up to. CI names
  each beta for a release still to come (`scripts/beta_version.sh`):
  `<version>-beta.<run>` while that version has no tag, and the next patch
  number once it does (2026.10.1 builds 2026.10.2-beta.<run>), so betas built
  between releases stay newer than the release panels run. Bar builds off
  `beta` are `<version>-beta.0`, so a bar panel flashed from one still takes
  the next beta.
- **Update URLs accept http://.** Every shipped build uses https; rejecting
  plain http in the schema is a policy choice (bench builds may use a local
  server).
- **Version strings of 24 characters or more** are cut in the saved update
  mark. Today's longest is about 21; keep CI's format short.

### Crash reports

- **A stale crash record can be saved twice** after a hardware watchdog
  reset on a panel no log client ever connected to. Clear the RAM record
  after saving it.
- **Only one record is kept**, so a later brownout clears an unread crash
  backtrace. Keeping the last report text separately needs a new preference
  key.
- **After a rollback**, the record names the firmware that saved it, not the
  version that crashed. Add the rolled-back version to the report.

### Device page

- `release_url` is now checked for https, but the page still compares
  versions with `parseInt`, which is looser than the firmware's parser for
  malformed versions.
- The favorites list's buttons are named "Move up", "Move down" and "Remove"
  without the team; add the team to their labels.
- A timezone pick shows on the Time line only at the next state read.

### CI, scripts and the site

- **Releases and betas do not wait for the size budget** (only pull requests
  do). Add `budget` to the publish jobs' `needs` if the budget should gate
  them.
- **A new push to `beta` can cancel a beta publish** in the middle of its
  uploads. Give the publish job its own concurrency without
  cancel-in-progress.
- **The tag publish and the site workflow** can deploy Pages at the same
  time; serialize them carefully (a plain shared group can cancel a pending
  release publish).
- **Every Pages deploy publishes the ELF files** (about 300 MB). The crash
  script reads ELFs from the release zips, so they could be dropped from the
  site.
- **A by-hand site run** deploys even while a release is pending, which can
  turn the next tag's deploy into a no-op.
- **The update mirror script's defaults** name another project's host and
  project; remove them before anyone sets mirror secrets.
- **Smaller:** `docs.yml` uses `actions/setup-node@v5` while `build.yml` uses
  `@v7`; `timezones_parsed.h`'s generator should include `<cstddef>`;
  `build_web.py` would drop a team name containing a quote; `tests/web`
  carries both `bun.lock` and `package-lock.json`;
  `check_site_firmware.sh` needs bash 4 and prints no usage.

### Docs and site

- **The docs' "Coming in the next release" asides** describe behavior `main`
  already has, since `main` is the full import. At the first release, remove
  each aside and rewrite the older text in front of it.
- **The privacy page** does not mention the public time servers the panel
  uses, and says "no crash reporting" while the panel keeps its own last
  crash. It carries an effective date, so a maintainer should make the edit
  and move the date.
- **The site's Twitter handle** (`@GameDayScore`) and the docs site's own
  address (`docs.gameday-scoreboard.com` in `docs/wiki/astro.config.mjs`)
  point outside this repository; point them at Apollo Automation's own or
  remove them.
- **A leftover file in `docs/site/.well-known/`** has no effect under the
  Pages project path; remove it unless a future domain needs it.
- **The variant table** in the DIY install page lists `moonhub75-2panel` as
  a variant; it is the build and installer folder name, while the YAML keeps
  `moonhub75`.

## Attempted and reverted

- **ESPHome Device Builder adoption** (`dashboard_import`): added, tested
  with a stub that fails on the guard images' paths, removed. The two-line
  path fix is verified; see "Further improvements".
- **Config validation for update channels and build types** (two commits):
  rejected configurations that no build uses, with no automated test of the
  validation. Dropped in review; re-add them with negative `esphome config`
  fixtures in CI.
- **A first over/under fix** also rejected numeric strings and booleans that
  read fine today; narrowed to the range check before committing.
- **A Python 3.9 compatibility change** to `release_notes.py`: the script
  still needs Python 3.10 elsewhere, so it was dropped before committing.
- **Two device page layout drafts** that put the keyboard order out of step
  with the visual order were redone before committing.

## Security options

The panel trusts the home network on purpose: OTA has no password, the
ESPHome API has no key, and the device page is open. This review kept that.
The options, with their costs:

1. **Keep it (today).** One-step setup, Home Assistant adds the panel with no
   key, and the device page works from any phone. Anyone on the network can
   change settings or install firmware; the panel must never be exposed to the
   internet (the README says so).
2. **Document network isolation.** Recommend an IoT network or VLAN in the
   docs. No firmware cost; protects only owners who follow it.
3. **An OTA password.** Blocks firmware installs from the network without the
   password. A password compiled into a public prebuilt image is public, so
   this only helps if each owner builds their own firmware or a per-panel
   password is set at setup, which needs new code and a place to show it.
   The panel's own update over HTTPS is not affected.
4. **An API encryption key.** Encrypts Home Assistant's connection and keeps
   other clients out. The same problem: a key in the public image is public.
   Owners who build their own get it today (the docs say how); prebuilt
   images would need per-panel provisioning. Adding a key also makes adoption
   ask for it.
5. **A device page login.** ESPHome's web server supports basic auth, but
   over plain HTTP on the local network it only stops casual changes, and it
   adds a login to every settings change and to the page's own requests. A
   default password in a public image would again be public.

The device page has no firmware upload (OTA is ESPHome's own and the
update feed over HTTPS). The low-cost step with real value is option 2 now,
and option 3 or 4 only together with per-panel secrets.

## Cutting the first Apollo release

1. **A final ESPHome.** `main` and release tags build only on a final
   release. When ESPHome 2026.10.0 (or later) ships, move `ESPHOME_VERSION`
   in `.github/workflows/build.yml` on `dev` in its own pull request, compile
   every build, run every check, and re-diff `components/improv_ble` against
   the new upstream component (it is a marked override of 2026.10.0b3's).
   The same pull request updates every other place that quotes the pin
   (`git grep 2026.10.0b3`): the DIY "Build it yourself" page, CONTRIBUTING.md's
   Setup and AGENTS.md.
2. **The release pull request.** A draft pull request into `dev` holds the
   rest: `version` 2026.10.1 in `firmware/gameday-common.yaml`, its
   `## v2026.10.1` section in `CHANGELOG.md` (a tag without a matching
   section fails CI), the docs with every "Coming in the next release" aside
   folded in, and the beta order above. Merge it once step 1 is on `dev`,
   after a last check that no change merged since needs a changelog line or
   an aside folded in.
3. **Promote.** Open "Promote dev to beta" (head `dev`, base `beta`), let the
   beta channel run, then "Promote beta to main" (head `beta`, base `main`).
   CI's Branch rules check the source branch and the final ESPHome. This
   first time, no panel can take the beta over the air, since the site only
   goes live with the first tag; test it by flashing test panels by hand
   from the beta run's `bar-*` artifacts or the `beta-firmware` prerelease's
   zips before promoting to `main`.
4. **Tag `main`.** `git tag v2026.10.1` on `main`'s merge commit and push the
   tag. The tag's Build run checks the tag against the version and the
   changelog, builds the three release variants, and its publish job
   assembles the site (docs/site, the firmware manifests and images, the
   rendered changelog, the beta channel folders), deploys it to GitHub Pages,
   and creates the GitHub release with the factory images and the manifest
   zips. The update mirror step warns and skips.
5. **The Pages environment.** The repository's `github-pages` environment
   allows deployments from `dev`, `main` and `v*` tags. Keep `main` and `v*`;
   `dev` can go, since only `main` (site.yml) and release tags (build.yml)
   publish the site.
6. **Check the live site.** After the deploy, read
   `https://apolloautomation.github.io/gameday-scoreboard-main/firmware/<variant>/manifest.json`
   for `moonhub75`, `moonhub75-2panel` and `scoreboard75` and check each
   names 2026.10.1. The `moonhub75-beta`, `scoreboard75-beta`,
   `beta/moonhub75` and `beta/scoreboard75` folders keep the last beta,
   2026.10.1-beta.<run>, until the next beta build (the two-panel build reads
   moonhub75's). Open the installer, the DIY installer and the changelog page,
   and check that a panel running a beta with Follow beta off (one flashed
   from the prerelease's `firmware-moonhub75-beta.zip` or
   `firmware-scoreboard75-beta.zip`) is offered the release with a working
   Release notes link. A bar panel is never offered a release.

`version` on `dev` stays 2026.10.1 until the next release's pull request.
Betas built in between are named 2026.10.2-beta.<run>, so panels on the
release with Follow beta on take them.

## End state

- **Merged into `dev`:** the fork setup
  ([#1](https://github.com/ApolloAutomation/gameday-scoreboard-main/pull/1))
  and this review
  ([#2](https://github.com/ApolloAutomation/gameday-scoreboard-main/pull/2)).
- **`beta`:** promoted from `dev` after the review merges. That push makes
  CI publish the `beta-firmware` prerelease on this repository, which stores
  the bar and Follow beta channels' files between site deploys.
- **`main`:** still at the import commit, because promoting to `main` needs
  a final ESPHome (or a maintainer's `esphome-beta-ok` label) and 2026.10 has
  no final yet.
- **No release and no `v*` tag.** Until the first release, the Pages address
  serves nothing: the installer pages, the update manifests and the changelog
  link return 404, and builds from this repository find no update (their
  Firmware box says "Not checked yet").

## Further improvements

In priority order:

1. **Device Builder adoption.** Done in
   [#4](https://github.com/ApolloAutomation/gameday-scoreboard-main/pull/4).
   Still open: a CI job that compiles a stub with only the package.
2. **The first Apollo release**, as above, once ESPHome 2026.10 ships.
3. **The beta version ordering.** Settled in the release pull request.
4. **A home for the docs.** Publish `docs/wiki` (for example under the Pages
   site with a base path) so its links between pages work, and point the
   README and the site at it.
5. **A domain and a setup QR code of the project's own.** The panel's setup
   QR code still opens another project's page; a short address would let the
   64 pixel code point at Apollo's setup page. Test the new code on a panel.
6. **List Game Day Scoreboard on the Apollo wiki's M-1 "Choose your firmware"
   page**, with the DIY installer as the M-1's install path. Written; it goes
   live with that wiki's next publish, after the first release.
7. **The protected-area recommendations** above, starting with the Live games
   scan refusal handling, the per-job fetch task, the short waits after Reset
   Wi-Fi, and the Follow beta and rollback edges.
8. **GitHub Discussions** with a "Show and tell" category, and the site's
   card for it.
9. **Negative config fixtures in CI**, then the dropped validation commits,
   and a check on the linker map that the restart log stays the only RTC
   variable.
10. **Cleanups:** the dead select plumbing, the Pages ELFs, `bun.lock`, the
    `setup-node` version in `docs.yml`.

## Final results

Every check, run on the review branch's last code commit (c3b25ef) and
compared with the import commit:

| Check | Import commit | Final |
|---|---|---|
| `esphome compile`, seven builds | 7 pass | 7 pass |
| Image check (update URLs in every `.bin`) | 5 fail (another project's hosts), 2 pass (demo builds have no update entity) | 7 pass, only the Pages address |
| `make -C tests` | 8,850 + 130 checks, 0 failures | 10,534 + 130 checks, 0 failures |
| `make -C tests replay` | Builds | Builds |
| `make -C tests fuzz`, UBSan only (local) | Passed that run; the over/under overflow reproduces on demand | Clean (fuzz_espn about 375,000 runs, fuzz_set about 15 million) |
| `make -C tests fuzz`, ASan and UBSan | CI passes; local runtime does not start | CI passes; local runtime does not start |
| `make -C tests heap` (host figures) | Within budget | Within budget; a soccer poll's other heap 768 to 496 B, NBA 560 to 520 B, NHL 512 to 496 B |
| Script self-tests (7) | Pass | Pass |
| Settings mock against `set_cases.json` | 68 cases | 80 cases, 0 failures |
| `site_seo.py check docs/site`, changelog render, bundle freshness | Pass | Pass |
| Device page tests | 54 pass | 69 pass |
| Docs build | CI passes (local Node too old) | CI passes (local Node too old) |
| Flash (OTA image), moonhub75 / scoreboard75 | 2,233,264 / 2,209,840 B | 2,236,832 / 2,213,408 B (+3,568 B, about 1.5 percent of the headroom) |
| Static internal RAM, moonhub75 / scoreboard75 | 158,472 / 157,672 B | 158,448 / 157,648 B |
| CI on the branch | Green | Green |
