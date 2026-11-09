---
title: "Baseball: minor leagues"
description: "Follow any Triple-A, Double-A, High-A or Single-A team: where its scores come from, what the board shows, doubleheaders and rainouts, and what's different from MLB."
sidebar:
  order: 6.5
---

:::note[Coming in the next release]
Minor league baseball comes with the next release. Everything on this page describes it.
:::

The panel can follow any of the 120 minor league teams at the four full-season levels: Triple-A, Double-A, High-A and Single-A. A minor league team gets the same board as an MLB team: the inning, the count, the outs, the runners and the run splashes. See [Baseball: MLB](/using/baseball/) for the board itself. This page covers what's different.

## Where the scores come from

ESPN doesn't carry minor league games, so for a minor league team the panel reads its schedule and scores from MLB's own stats service (statsapi.mlb.com), and its logos from MLB's image server (midfield.mlbstatic.com). Every other league still comes from ESPN. The requests carry the team you picked and nothing about you.

The two are kept apart. If MLB's service is slow, down or refuses the panel, the panel waits longer before asking it again, on its own schedule, and your other teams and leagues keep updating from ESPN as usual. The same goes the other way. While it can't reach MLB and has no game to show, the board says **Can't reach MLB**.

## Picking a team

Tap **Change team** on the device page and open the **MiLB** tab. The teams are under a heading for each level, Triple-A first, and each tile says its level. Search finds a team by its name, its city or its level, like "double-a". See [Pick your team](/get-started/pick-your-team/).

Home Assistant's **Team** list leaves minor league teams out, since 120 more would make the list too long for one entity. Pick them on the device page. Once you do, Home Assistant's **Game Status** follows the game like any other. **Last Play** stays empty for a minor league team, since the panel gets no plays from MLB (below).

Minor league teams can be [favorites](/using/favorites/) too, mixed with teams from any other league.

## Before the game

The next-game card shows both logos and the records. The ticker carries the first pitch day and time, the probable starting pitchers when MLB lists both, and the ballpark. MLB doesn't list a betting line or a TV channel for minor league games.

The Up next list on the device page shows the team's next three games in the coming ten days.

## During the game

The clock row shows the inning, like "Top 5th", "Mid 5th" or "Bot 9th", or the delay, like "Rain Delay", "Lightning Delay" or "Delayed". During a delay the count and runners wait, and the ticker shows each team's hits and errors. A replay review or a manager's challenge doesn't stop the inning on the board.

The panel reads the game every 10 seconds, about 1.4 KB each time.

A few things MLB's feed doesn't have:

- **Team colors.** The abbreviations, the splashes and the off-season clock are white.
- **The last play.** The ticker has the count, the outs, the pitcher and the batter, but no play.
- **How a run scored.** Every run gets "RUN!" or "2 RUNS!", never the "HOME RUN" splash with fireworks.

## The final

The ticker says "Final", "Final/10" after extra innings, or "Final/7" for a game called early, with each team's hits and errors and the records. A victory splash with fireworks says your abbreviation and "WINS". The final stays up for ten minutes, then the panel looks up the next game.

## Doubleheaders and rainouts

Game 2 of a doubleheader has no start time of its own: it starts about half an hour after game 1 ends. The panel shows it as "Today TBD" and checks it every minute that day, so it picks game 2 up as soon as it starts.

A postponed, canceled or suspended game shows "Postponed", "Canceled" or "Suspended" and gets no victory splash. After ten minutes the panel moves on to the team's next game. A postponed game comes back on the date it was moved to.

## Between seasons

When the team's next game is more than 10 days away, the board shows the clock, with the opener of the next season under it once MLB has the schedule, like "Next: DUR vs GWN Mar 26 6:35 PM". See [When your team has no game coming](/using/modes/#when-your-team-has-no-game-coming).

## Live games and On now

**Live games** has a **MiLB** button. It follows games at all four levels; you can't pick one level. When nothing is live and no team is picked, the finals rotation shows minor league finals with the other leagues' and the games still to come today. See [Modes](/using/modes/).

The **On now** tab in the team picker lists today's minor league games too. Your browser reads them from MLB's stats service, the way it reads the other leagues from ESPN.

## Data use

The **ESPN Data** sensor in Home Assistant and **Data used** on the device page count what the panel downloads from MLB as well as from ESPN. The device page's Diagnostics show **Last MLB read**, how the last read from MLB went and how long ago, once the panel has read minor league scores.
