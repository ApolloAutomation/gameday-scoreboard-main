---
title: "Hockey: AHL and ECHL"
description: "Follow any AHL or ECHL team: where its scores come from, what the board shows, the logos, and what's different from the NHL."
sidebar:
  order: 8.5
---

:::note[Coming in the next release]
The AHL and the ECHL come with the next release. Everything on this page describes them.
:::

The panel can follow any of the AHL's 32 teams and the ECHL's 30. An AHL or ECHL team gets the same board as an NHL team: the period and the clock, overtime, the shootout and the goal splashes. See [Hockey: NHL](/using/hockey/) for the board itself. This page covers what's different.

## Where the scores come from

ESPN doesn't carry either league, so for an AHL or ECHL team the panel reads its schedule and scores from HockeyTech (lscluster.hockeytech.com), the stats service both leagues use for their own websites, and its logos from HockeyTech's logo server (assets.leaguestat.com). Every other league still comes from ESPN, and minor league baseball from MLB. The requests carry the team or game you're following and the league's public site key, the same one the league's own website sends, and nothing about you.

HockeyTech is kept apart from the others. If it's slow, down or refuses the panel, the panel waits longer before asking it again, on its own schedule, and your other teams and leagues keep updating as usual. The same goes the other way. While it can't reach HockeyTech and has no game to show, the board says **Can't reach HockeyTech**.

If a league ever changes its site key, a firmware update brings the new one. DIY builders can set it themselves: the keys and the leagues' client codes are the `hockeytech_ahl_key`, `hockeytech_ahl_client`, `hockeytech_echl_key` and `hockeytech_echl_client` settings at the top of `firmware/gameday-common.yaml`.

## Picking a team

Tap **Change team** on the device page and open the **AHL** or **ECHL** tab. Search finds a team by its name or its city from any tab. See [Pick your team](/get-started/pick-your-team/).

Home Assistant's **Team** list has them too, like "AHL: Syracuse Crunch". **Last Play** stays empty for an AHL or ECHL team, since the panel gets no plays from HockeyTech (below).

AHL and ECHL teams can be [favorites](/using/favorites/) too, mixed with teams from any other league.

## Before the game

The next-game card shows both logos and the records: wins, regulation losses, and overtime and shootout losses together, the way the NHL counts them. The ticker carries the puck drop day and time and the arena. HockeyTech doesn't list a betting line or a TV channel.

The Up next list on the device page shows the team's next three games.

## Logos

The panel shows HockeyTech's small team logos. A team whose logo HockeyTech has no small copy of, like the Greenville Swamp Rabbits at the moment, shows its abbreviation in a box in the logo's place instead. The device page shows that team's full-size logo.

## During the game

- **The clock and period** under the scores, like "12:34 2nd".
- **Overtime** reads "OT". In the playoffs, where overtime goes on until someone scores, later periods read "2OT", "3OT" and so on.
- **A shootout**, in the regular season, reads "SO".
- **Intermissions** read "End of 1st", "End of 2nd" and so on, and the ticker shows the records.

The panel reads the game every 10 seconds, about 2 KB each time. HockeyTech's server can hand out the same reading for up to a minute, so the board can be about a minute behind the arena.

A few things HockeyTech's feed doesn't have:

- **Team colors.** The abbreviations, the splashes and the off-season clock are white.
- **The last play.** The ticker shows the records during intermissions, but no plays.
- **Power plays**, as with the NHL.

## The final

The ticker says "Final", "Final/OT", "Final/SO" or "Final/2OT", with the records. A victory splash with fireworks says your abbreviation and "WINS". The final stays up for half an hour in My team, then the panel looks up the next game. **Refresh scores** during that half hour reads the final again and keeps it up.

A postponed, canceled or suspended game shows "Postponed", "Canceled" or "Suspended" and gets no victory splash. The panel moves on to the team's next game.

## Between seasons

When HockeyTech lists no game for the team, or its next game is more than 10 days away, the board shows the clock, with the next game under it once HockeyTech has the new season's schedule. The panel checks for it every 6 hours. See [When your team has no game coming](/using/modes/#when-your-team-has-no-game-coming).

## Live games and On now

**Live games** has an **AHL** button and an **ECHL** button. When nothing is live and no team is picked, the finals rotation shows their finals with the other leagues' and the games still to come today. See [Modes](/using/modes/).

The **On now** tab in the team picker doesn't list AHL or ECHL games. Your browser reads that list itself, and HockeyTech's feed answers the panel but not a web page on another site.

## Data use

The **ESPN Data** sensor in Home Assistant and **Data used** on the device page count what the panel downloads from HockeyTech as well. The device page's Diagnostics show **Last HockeyTech read**, how the last read from HockeyTech went and how long ago, once the panel has read AHL or ECHL scores.
