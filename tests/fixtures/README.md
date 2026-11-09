# Test fixtures

Recorded from ESPN on 2026-09-05 with a `curl/8.0` User-Agent (ESPN returns
403 to most other agents).

| File | Source |
| --- | --- |
| `team_nfl_dal.json` | `.../football/nfl/teams/dal` |
| `team_ncaa_bc.json` | `.../football/college-football/teams/103` |
| `scoreboard_nfl.json` | `.../football/nfl/scoreboard` (week 1, all pre) |
| `schedule_nfl_dal.json` | `.../football/nfl/teams/6/schedule`, trimmed to the first 5 events (recorded 2026-09-08) |
| `schedule_ncaa_bc.json` | `.../football/college-football/teams/103/schedule`, trimmed to the first 5 events (recorded 2026-09-08; the first is already final) |
| `scoreboard_ncaa_live.json` | `.../football/college-football/scoreboard?groups=80&limit=200` (mix of pre, in, post) |
| `scoreboard_ncaa_conf_titles.json` | `.../college-football/scoreboard?groups=80&limit=300&dates=20251206`, conference title games, all final (recorded 2026-10-08) |
| `scoreboard_ncaa_cfp_quarterfinals.json` | `.../college-football/scoreboard?groups=80&limit=300&dates=20260101`, CFP quarterfinals at neutral sites (recorded 2026-10-08) |
| `scoreboard_ncaa_cfp_final.json` | `.../college-football/scoreboard?groups=80&limit=300&dates=20260119`, CFP national championship (recorded 2026-10-08) |
| `scoreboard_nfl_wild_card.json` | `.../football/nfl/scoreboard?dates=20260110`, two wild card games (recorded 2026-10-08) |
| `scoreboard_nfl_super_bowl.json` | `.../football/nfl/scoreboard?dates=20260208`, Super Bowl LX (recorded 2026-10-08) |
| `event_ncaa_live.json` | `.../college-football/scoreboard/401871053`, one game on its own: FIU @ JXST, 0:33 left in the 4th (recorded 2026-10-13) |
| `event_nfl_pre.json` | `.../football/nfl/scoreboard/401872999`, CHI @ ATL a week out, with odds (recorded 2026-10-13) |
| `event_nfl_super_bowl.json` | `.../football/nfl/scoreboard/401772988`, Super Bowl LX on its own (recorded 2026-10-13) |

Event ids the tests rely on are listed at the top of `test_parse.cpp`.

MLB, recorded 2026-10-07 (postseason) from the single-event endpoint
`.../baseball/mlb/scoreboard/<event id>` that per-event leagues poll:

| File | Source |
| --- | --- |
| `event_mlb_top.json` | 401908016 LAD @ ATL, Top 7th, 2-1 count, two out, runner on second |
| `event_mlb_bottom.json` | 401908016, Bot 6th, 1-1 count, one out, runner on second |
| `event_mlb_end.json` | 401908016, End 5th (stale count left in the situation) |
| `event_mlb_final.json` | 401907992 CLE @ CHW, Final 9-3, series line |
| `event_mlb_pre.json` | 401908005 MIL @ SD, pre-game with probable pitchers and odds |
| `event_mlb_hr_before.json`, `event_mlb_hr.json` | 401907987 TB @ NYY, the polls either side of a two-run home run (Top 6th) |
| `event_mlb_postponed.json` | 401814798 CHC @ CLE, 2026-04-04, rained out: state `post` with `completed` false and status `STATUS_POSTPONED` (recorded after the fact) |
| `team_mlb_atl.json` | `.../baseball/mlb/teams/15` (next event is the game in progress) |

Soccer, from the single-event endpoint `.../soccer/<league>/scoreboard/<event id>`.
Brazilian Serie A (`bra.1`, the same document shape as MLS and the Premier
League) recorded live 2026-10-08; the rest recorded 2026-10-08 from past and
upcoming games:

| File | Source |
| --- | --- |
| `event_soccer_first_half.json` | 401841249 SAO @ CRU, 25', 0-0, a yellow card each |
| `event_soccer_halftime.json` | 401841257 VAS @ BOT, HT, VAS 1-0 |
| `event_soccer_second_half.json` | 401841253 MIR @ BRA, 90', 1-1, MIR red card at 28' |
| `event_soccer_stoppage.json` | 401841255 COR @ INT, 90'+2', INT 2-1, status name `STATUS_IN_PROGRESS` |
| `event_soccer_full_time.json` | 401841255, FT, INT win 2-1 |
| `event_epl_pre.json` | `soccer/eng.1` 401879268 LEE @ ARS, pre-game with the three-way moneyline |
| `event_mls_pre.json` | `soccer/usa.1` 761847 DC @ MIA, pre-game |
| `event_epl_final.json` | 401878779 ARS @ SUN, FT 0-2, SUN red card, ARS penalty at 90'+7' |
| `event_mls_final.json` | 761829 SD @ MIA, FT 2-2 (a draw) |
| `event_soccer_pens.json` | `soccer/uefa.champions` 401862897 ARS @ PSG, 2026 final, 1-1, PSG win 4-3 on penalties |
| `event_soccer_aet.json` | `soccer/fifa.world` 760500 CPV @ ARG, AET 3-2, own goal at 111' |
| `team_epl_ars.json` | `.../soccer/eng.1/teams/359` |
| `team_mls_mia.json` | `.../soccer/usa.1/teams/20232` |
| `team_ucl_ars.json` | `.../soccer/uefa.champions/teams/359`: the same club's next Champions League game |
| `team_uel_ars.json` | `.../soccer/uefa.europa/teams/359`: a club not in the competition gets HTTP 200 with no `nextEvent` |
| `schedule_mls_mia.json` | `.../soccer/usa.1/teams/20232/schedule?fixture=true`, trimmed to the first 2 events (without `fixture=true` the endpoint lists past results) |
NBA (preseason) and WNBA (semifinals), recorded 2026-10-07 from
`.../basketball/{nba,wnba}/scoreboard/<event id>`:

| File | Source |
| --- | --- |
| `event_nba_live.json` | 401914123 MIN @ IND, 8:27 3rd, 74-73 |
| `event_nba_final.json` | 401914123, Final 123-112 |
| `event_nba_clock.json` | 401908620 MIL @ OKC, 12.3 1st (tenths, no minutes) |
| `event_nba_end.json` | 401908620, End of 1st |
| `event_nba_pre.json` | 401914129 GS @ POR, pre-game with odds |
| `event_wnba_live.json` | 401918297 NY @ ATL, 4.5 2nd, ATL leads series 1-0 |
| `event_wnba_half.json` | 401918297, Halftime |
| `event_wnba_ot.json` | 401918297, 20.8 OT |
| `event_wnba_final.json` | 401918297, Final/OT 101-98 (series line not yet updated) |
| `team_nba_ind.json` | `.../basketball/nba/teams/11` (next event is the game in progress) |
| `team_wnba_ny.json` | `.../basketball/wnba/teams/9` (next event is the game in progress) |

Men's college basketball, recorded 2026-10-07 between seasons from
`.../basketball/mens-college-basketball/scoreboard/<event id>`. ESPN serves a
past game only in its final state, so there is no live college fixture:

| File | Source |
| --- | --- |
| `event_mcbb_final.json` | 401856600 CONN @ MICH, 2026 national championship, Final 69-63 |
| `event_mcbb_ot.json` | 401851437 PENN @ YALE, Ivy League final, Final/OT 88-84 |
| `event_mcbb_pre.json` | 401925733 OAK @ MICH, 2026-27 opener, pre-game |
| `team_mcbb_mich.json` | `.../basketball/mens-college-basketball/teams/130` (next event is the opener) |
NHL, recorded 2026-10-07 and 2026-10-08 UTC from the single-event endpoint
`.../hockey/nhl/scoreboard/<event id>`:

| File | Source |
| --- | --- |
| `event_nhl_live.json` | 401892455 COL @ WPG, 3:21 left in the 1st, 0-0, a delay of game penalty as the last play |
| `event_nhl_end.json` | 401891830 PIT @ WSH, End of 1st (status END_PERIOD, the intermission), 2-1 WSH |
| `event_nhl_final.json` | 401891830 PIT @ WSH, Final 5-3 WSH |
| `event_nhl_pre.json` | 401892456 EDM @ ANA, pre-game with odds and O/U |
| `event_nhl_final_so.json` | 401803369 PIT @ CAR, 2026-03-10, Final/SO 5-4 CAR (recorded after the fact) |
| `event_nhl_final_2ot.json` | 401869781 OTT @ CAR, 2026 playoffs, Final/2OT 3-2 CAR, series line (recorded after the fact) |
| `team_nhl_edm.json` | `.../hockey/nhl/teams/6` (next event is the EDM @ ANA game above) |

`football_golden.txt` is written by the host tests (`GOLDEN_WRITE=1`) and
pins football's output; see `golden_football.cpp`.

Next-game look-ahead, recorded 2026-10-08 ~14:50Z:

| File | Source |
| --- | --- |
| `team_nhl_pit_post.json` | `.../hockey/nhl/teams/16`, still naming the 10/7 final the next morning |
| `day_nhl_20261009.json` | `.../hockey/nhl/scoreboard?dates=20261009` (PIT @ CBJ among four games) |
| `day_mlb_20261009.json` | `.../baseball/mlb/scoreboard?dates=20261009` (an off day: no games) |

Recorded 2026-10-12 during LAD @ MIL (postseason, Bot 3rd), when ESPN's team
endpoint had already moved on to the next game:

| File | Source |
| --- | --- |
| `team_mlb_mil_live.json` | `.../baseball/mlb/teams/8`, nextEvent is 401908032 MIL @ LAD on 10/14 |
| `day_mlb_20261012.json` | `.../baseball/mlb/scoreboard?dates=20261012`, 401908031 LAD @ MIL in progress, CLE @ TB pre |

Soccer does the same. Recorded 2026-10-12 during CRU @ BRA in Brazil's Serie A
(25', no MLS or Premier League game was on). The team's fixture list
(`.../schedule?fixture=true`) still listed the game on now first:

| File | Source |
| --- | --- |
| `team_bra_cru_live.json` | `.../soccer/bra.1/teams/2022`, nextEvent is 401841276 CRU @ GRE on 10/18 |
| `day_bra_20261012.json` | `.../soccer/bra.1/scoreboard?dates=20261012`, 401841260 CRU @ BRA in progress, CAP @ CHA and BOT @ CFC full time |

College football does the same. Recorded 2026-10-17 at 12:40 PM Central
during FLA @ TEX (halftime):

| File | Source |
| --- | --- |
| `team_ncaa_tex_live.json` | `.../football/college-football/teams/251`, nextEvent is 401856732 MISS @ TEX on 10/24, group 8 (SEC) |
| `day_ncaa_20261017.json` | `.../college-football/scoreboard?groups=8&dates=20261017`, 401856724 FLA @ TEX and MSST @ LSU in progress, five SEC games to come |

College football games with no kickoff time yet, recorded 2026-10-14. ESPN
lists them at midnight Eastern with `timeValid: false` and a `shortDetail` of
"TBD" (Iowa, team 2294, is off on 10/17, so its next game is one of them):

| File | Source |
| --- | --- |
| `team_ncaa_iowa_tbd.json` | `.../football/college-football/teams/2294`, nextEvent is 401858497 IOWA @ MINN on 10/24, time TBD |
| `scoreboard_ncaa_tbd.json` | `.../college-football/scoreboard?groups=5&dates=20261024`, three TBD games and three with times (the day before, 20261023, has none) |
| `event_ncaa_tbd.json` | `.../college-football/scoreboard/401858497`, the TBD game on its own |
| `schedule_ncaa_iowa_tbd.json` | `.../college-football/teams/2294/schedule`, trimmed to events 6 to 12: the 10/9 final, five TBD games, then 11/27 at 12:00 PM EST |

Recorded 2026-10-15 at 9:41 AM Central (NBA preseason), for the rotation's
later-today cards:

| File | Source |
| --- | --- |
| `day_nba_20261015.json` | `.../basketball/nba/scoreboard?dates=20261015`, two games to come: TOR @ NY (no TV listed) and OKC @ HOU (NBA TV) |

Golf, recorded 2026-11-07 03:18Z from a tour's scoreboard header
(`https://site.api.espn.com/apis/v2/scoreboard/header?sport=golf&league=<tour>`)
and season calendar (`.../golf/<tour>/scoreboard?dates=`). The DP World Tour,
Korn Ferry Tour, PGA Tour Champions and LIV headers have the same shape as the
PGA Tour's and stand in for states the two tours the panel follows weren't in:

| File | Source |
| --- | --- |
| `golf_header_pga_suspended.json` | `league=pga`: 401850979 World Wide Technology Championship, round 2 suspended (`STATUS_SUSPENDED`, state `post`, completed false), every listed player finished the round, ties at T4 and T6 |
| `golf_header_lpga_live.json` | `league=lpga`: 401835175 TOTO Japan Classic, round 3 in progress, a T1 tie, thru 4 to 14, only 21 players listed |
| `golf_header_eur_after_round.json` | `league=eur`: 401822708 Abu Dhabi Aldar Championship, round 2 play complete (`STATUS_PLAY_COMPLETE`, state `post`, completed true while the event goes on), round 3 tee times |
| `golf_header_kft_final.json` | `league=ntw`: 401837451 Korn Ferry Tour Championship, final, the winner's `winner` flag false |
| `golf_header_champions_pre.json` | `league=champions-tour`: 401832050 Charles Schwab Cup Championship, scheduled, no competitors yet |
| `golf_header_liv_canceled.json` | `league=liv`: 401843582 LIV Golf Michigan, canceled |
| `golf_header_none.json` | `https://site.api.espn.com/apis/v2/scoreboard/header?sport=golf&league=pga&dates=20261103`, the Tuesday before the World Wide Technology Championship, a day with no PGA Tour round: `leagues[0]` has no `events` |
| `golf_calendar_pga_2026.json` | `.../golf/pga/scoreboard?dates=20261103`, a Tuesday with no round: no events, the 2026 season's calendar of 49 events |
| `golf_calendar_pga_2027.json` | `.../golf/pga/scoreboard?dates=2027`, trimmed to its first 2 events; the 2027 calendar is whole |
| `golf_calendar_lpga_2027.json` | `.../golf/lpga/scoreboard?dates=2027`, trimmed the same way |

Racing, recorded 2026-11-07 05:20Z from a series' scoreboard header
(`https://site.api.espn.com/apis/v2/scoreboard/header?sport=racing&league=<f1|nascar-premier>`,
one entry per session of a race weekend, all with the weekend's event id) and
season calendar (`.../racing/<series>/scoreboard?dates=`, the coming Tuesday's).
"Trimmed" means each competitor's `headshot`, `logo`, `records` and `uid`, and
each entry's `links`, `appLinks`, `guid`, `geoBroadcasts` and `track`, are
dropped; nothing the panel reads changes:

| File | Source |
| --- | --- |
| `racing_header_f1_practice.json` | `league=f1`: 600057448 Sao Paulo Grand Prix weekend on Saturday morning, FP1 and FP2 final (22 and 21 drivers, lap times, no `behindTime` on most rows), FP3, Qualifying and the Race scheduled with no competitors. Trimmed |
| `racing_header_f1_final.json` | `league=f1&dates=20261025`: 600057446 United States Grand Prix, every session final, the race with `behindTime`, `behindLaps` and three `STATUS_RETIRED` rows. Whole, 93 KB (the heap budget's read) |
| `racing_header_f1_sprint.json` | `league=f1&dates=20261011`: 600057445 Singapore, a sprint weekend, trimmed to its FP1, Sprint Shootout (`SS`) and Sprint Race (`SR`) entries |
| `racing_header_f1_sprint_shootout.json` | The same, trimmed to FP1 and the Sprint Shootout, whose `behindTime` is each driver's own lap time and whose rows are all `STATUS_UNKNOWN` |
| `racing_header_f1_live.json` | Not recorded: `racing_header_f1_final.json` with the Race entry set on lap 34 (`fullStatus.period` 34, `STATUS_IN_PROGRESS`, state `in`, shortDetail "Lap 34", no `flag`) and every `winner` false. Trimmed. No session was on track while these were recorded |
| `racing_header_f1_canceled.json` | `league=f1&dates=20260412`: 600057430 Bahrain Grand Prix, all five sessions `STATUS_CANCELED` (state `post`), no competitors. Whole |
| `racing_header_nascar_pre.json` | `league=nascar-premier`: 202611080042 NASCAR Cup Series at Miami, the season finale, scheduled (`laps` "267"), no competitors. NASCAR lists only the race, with no `competitionType`. Whole |
| `racing_header_nascar_final.json` | `league=nascar-premier&dates=20261101`: 202611010020 at Martinsville, final, 37 drivers: no per-driver status, `behindTime` ".000", `behindLaps` negative. Trimmed |
| `racing_header_nascar_duels.json` | `league=nascar-premier&dates=20260212`: Daytona's Duel #1 and Duel #2, two events on one day (`competitionType` 4, Exhibition), both final. Trimmed |
| `racing_header_none.json` | `league=f1&dates=20261110`, a Tuesday with no session: `leagues[0]` has no `events` |
| `racing_calendar_f1_2026.json` | `.../racing/f1/scoreboard?dates=20261110`: no events, the 2026 calendar of 25 races. Entries have no `id` (it is in `event.$ref`), and their times run 3 hours after the header's |
| `racing_calendar_nascar_2026.json` | `.../racing/nascar-premier/scoreboard?dates=20261110`: the 2026 calendar of 40 races, exhibitions included |

Minor league baseball, from the MLB Stats API's schedule endpoint
(`https://statsapi.mlb.com/api/v1/schedule`), recorded 2026-11-08 between
22:50 and 23:30 UTC with a `curl/8.0` User-Agent, each with the exact query
`source_mlbstats.cpp` builds (its `fields=` list cuts the document on the
server; `test_mlbstats.cpp` checks the URLs). Minor league baseball was
between seasons, so the live documents are winter league games (sport 17,
the Dominican, Venezuelan, Puerto Rican and Mexican Pacific leagues), which
the same endpoint serves in the same shape; the team, far, final and
postponed documents are minor league ones. The two team reads were recorded
again at 00:30 UTC on 2026-11-09, when the team read's `fields=` list gained
each date's own `date`; Durham's games are the same as before:

| File | Source |
| --- | --- |
| `mlbstats_team_dur_202604.json` | Team read, Durham Bulls (234, Triple-A), `startDate=2026-04-06&endDate=2026-04-17`: 815849 at SWB postponed 4/7 (cold), listed again 4/10 as game 2 of a doubleheader (no start time, postponed again) and 4/11 as game 2 (final), a doubleheader each day. Every game over; the tests set the ones from a given day on back to Scheduled to read it as on that morning |
| `mlbstats_team_eug_202605.json` | Team read, Eugene Emeralds (461, High-A), `startDate=2026-05-22&endDate=2026-06-02`: under 5/22, after that night's game, 864127 TRI @ EUG, game 2 with no start time, still Scheduled, its `gameDate` 2027-05-19. Of the 8,718 games the four levels list for 2026, the only one more than 30 hours from midnight UTC of its date. Every real game over |
| `mlbstats_team_none.json` | Team read, Durham, `startDate=2026-11-07&endDate=2026-11-18`: `{"dates":[]}` between seasons |
| `mlbstats_far_dur.json` | Far read, Durham, `sportId=11&startDate=2026-11-19&endDate=2027-07-06`: next season's first 88 games, the opener 860674 on 2027-03-26 |
| `mlbstats_game_final.json` | Game read, 816688 MEM @ DUR 2026-08-14, Final 12-2, probable pitchers, the last count and runners still in the line score |
| `mlbstats_game_final_extra.json` | 815051 RNO @ RR 2026-08-01, Final 9-8 in 11 |
| `mlbstats_game_final_short.json` | 816400 MEM @ JAX 2026-08-02, called in the 7th of 9 for rain (statusCode FR) |
| `mlbstats_game_postponed.json` | 815849 DUR @ SWB: three dated entries, postponed (DC), postponed (DI, game 2, no start time), final |
| `mlbstats_game_pregame.json` | 850461 MXC @ MOC, Pre-Game, the line score already there with an empty count |
| `mlbstats_game_scheduled.json` | 850462 HER @ GSV, Scheduled, no line score |
| `mlbstats_game_top.json` | 851475 LIC @ ESC, Top 6th, 2-0, two out, runner on third |
| `mlbstats_game_middle.json` | 851475, the next read: Middle 6th, LIC scored a run in between |
| `mlbstats_game_loaded.json` | 851475, Bot 6th, bases loaded after a walk (the count still 4-2) |
| `mlbstats_game_bottom.json` | 870006 MAR @ ORI, Bot 9th, 2-2, none out |
| `mlbstats_game_end.json` | 870006, End 9th, MAR 10-6 |
| `mlbstats_game_over.json` | 870006, the next read: Game Over (codedGameState O), MAR wins |
| `mlbstats_game_delay.json` | 851497 AGU @ GIG, rain delay in the Top 6th (statusCode IR, reason Rain) with runners on second and third |
| `mlbstats_day_winter.json` | Day read, `sportId=17&date=2026-11-08` at 23:16 UTC: eight games on (one in a rain delay), one final, seven to come (one listed for 21:21 UTC still Scheduled) |
| `mlbstats_day_milb_20260814.json` | Day read at all four levels, `date=2026-08-14`: 58 finals (one in 11 innings) and three postponed, listed in start order with the postponed ones last |

The AHL and the ECHL, from HockeyTech's feed
(`https://lscluster.hockeytech.com/feed/index.php`), recorded 2026-11-09
between 01:12 and 01:28 UTC (Sunday evening in the East) with a `curl/8.0`
User-Agent, each with the exact query `source_hockeytech.cpp` builds
(`test_hockeytech.cpp` checks the URLs) and the leagues' public site keys from
`firmware/gameday-common.yaml`. Every game of the weekend was over and none
was on, so there is no live document: the tests set a recorded clock's live
fields (status 2, the period, the clock left, the words) the way the feed
sends them during play. Hockey has no doubleheaders. The feed's server caches
the scorebar and the day for four minutes and the game centre's clock for one
(`Cache-Control`, `X-Cache-Status`):

| File | Source |
| --- | --- |
| `ht_team_ahl_syr.json` | Team read (`view=scorebar`, `team_id=324`, one game back and five ahead), Syracuse Crunch: 1029277 HFD @ SYR 11/7, Final OT 3-2, then five games to come from 1029294 LAV @ SYR on 11/11. Its counts are games, not days, around the league's own today |
| `ht_team_echl_rc.json` | Team read, Rapid City Rush (70): 25606 RC @ IDH 11/7 postponed (status 8, the third postponed game in a row there), then five to come from 25626 TAH @ RC on 11/11 |
| `ht_game_ahl_pre.json` | Game read (`feed=gc&tab=clock`), 1029294 LAV @ SYR, not started (status 1, progress " 7:00 PM") |
| `ht_game_ahl_final.json` | 1029271 CLE @ LAV, Final 4-2 |
| `ht_game_ahl_final_ot.json` | 1029277 HFD @ SYR, Final OT 3-2, the clock at 2:01 left in the overtime |
| `ht_game_echl_final_ot.json` | 25617 CIN @ IND, Final OT1 3-2 (the ECHL's words), period name "1st OT" |
| `ht_game_echl_final_so.json` | 25591 BLM @ FW 2026-11-06, Final SO 5-4: period 4 ("1st OT") with an `SO` entry in the scoring |
| `ht_game_echl_postponed.json` | 25606 RC @ IDH, postponed (status 8) |
| `ht_day_ahl_20261107.json` | Day read (`view=gamesbydate&fetch_date=2026-11-07`), the AHL's Saturday: 16 finals, four in overtime, every goal listed. Whole, 103 KB (the heap budget's day read) |
| `ht_day_echl_20261107.json` | The ECHL's Saturday: 13 finals, three in overtime, and RC @ IDH postponed |
| `ht_day_echl_20261110.json` | The ECHL's Tuesday, read two days ahead: two games to come, one at 10:30 AM Eastern |

Not from ESPN: `set_cases.json` is a table of `POST /gameday/set` requests and
the settings they leave in `/gameday/state`. `test_set.cpp` runs it through
`set_request.h`, and `tests/web/mock_device.py --selftest` through the device
page's mock, so the two answer the same way. Add a case there when a set key
or its limits change.
