---
title: "Baseball: MLB"
description: "What the panel shows for an MLB game: the inning, the count, outs, the runners on a diamond, run and home run splashes, and the final."
sidebar:
  order: 6
---

MLB games get their own board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

:::note[Coming in the next release]
Minor league teams, at Triple-A, Double-A, High-A and Single-A, get this same board, with their scores from MLB instead of ESPN. See [Baseball: minor leagues](/using/minor-league-baseball/) for what's different.
:::

## Before the game

The next-game card shows both logos, your team on the left, with the records under them. The ticker carries:

- the first pitch day and time in the panel's timezone
- the series, when ESPN has one
- the probable starting pitchers for both teams, when ESPN lists both
- the betting line, over/under and TV channel, when ESPN has them and **Pre-game odds and TV** is on
- the ballpark

MLB has no Up next list on the device page: a team's season schedule is too large for the panel to read.

## During the game

- **The inning** in the clock row, in ESPN's words, like "Top 5th", "Mid 5th" or "Rain Delay".
- **The count and outs**: the balls and strikes to the left of a small diamond and the outs, like "2 OUT", to its right.
- **The runners**: the diamond's bases light gold for each runner. With the bases loaded they turn red.
- **The ticker** with the count, the outs, the pitcher and the batter, and the last play. Turn them off with **Count and at-bat** and **Last play** in the Ticker card. See [The ticker](/using/ticker/).

Between half innings the ticker shows each team's hits and errors instead, with the series when there is one.

On a single 64x64 panel the diamond sits between the two scores, with the outs as three dots under it. The count is in the ticker.

## Runs and home runs

When the score changes, the panel fills the screen for 4 seconds in the scoring team's color:

| Score | Your team | The other side |
| --- | --- | --- |
| One run | RUN | the abbreviation and SCORES, like "NYY SCORES" |
| Two or more runs in one update | the number and RUNS, like "2 RUNS" | the abbreviation and the runs, like "NYY 2 RUNS" |
| Home run | HOME RUN, with fireworks | the abbreviation and HOME RUN, with fireworks |

A home run gets fireworks on black with the words in the team's color. Other runs flash the screen in the team's color. The other side's splashes need **Opponent scores too** on.

When your team wins, a victory splash with fireworks says your abbreviation and "WINS".

:::note[Coming in the next release]
Two more splashes, both with fireworks:

- **GRAND SLAM**: a home run that ESPN says scored four runs, or one ESPN's play calls a grand slam. Runs from an earlier play in the same update don't make a home run a grand slam.
- **WALK-OFF**: the home team goes ahead in the bottom of the 9th inning or later, which ends the game. Your walk-off says "WALK-OFF" and the other side's says its abbreviation first, like "NYY WALK-OFF". When ESPN marks the game final in the same update, the walk-off takes the victory splash's place. When ESPN marks it final an update later, the walk-off shows first and the victory splash follows. If the panel missed every update from the 8th inning to the final, it can't tell where the lead came from, so the final gets only its usual splash.

Runs, home runs, grand slams and walk-offs each have a switch in the Celebrations card. With **Grand slam** off, a grand slam shows as a home run, and with **Home run** off, as runs. See [Celebrations](/using/device-page/#celebrations).

A minor league game gets runs and walk-offs only. MLB's minor league scores don't say how a run scored, so a home run or a grand slam there shows as runs, under the **Runs** switch. In the 7-inning games of a minor league doubleheader, a walk-off counts from the bottom of the 7th. See [Baseball: minor leagues](/using/minor-league-baseball/).
:::

:::note[Coming in the next release]
A big play in another baseball game can take the board for a minute, with **Switch to big plays in other games** on. The panel checks those games about every 45 seconds, and by then ESPN's last play is often the next batter's, so a home run is also told from the score and the bases: the batting team scored its runners plus one, the bases are now empty and the outs haven't changed. With the bases loaded before, that's a grand slam. Other runs show as runs, so turn off **Runs** if a switch for every run is too much. A walk-off counts when the panel saw the game in the 9th inning or later. See [Big plays in other games](/using/modes/#big-plays-in-other-games).
:::

## The final

The ticker says "Final", or "Final/10" after extra innings, with each team's hits and errors and the series or the records. The final stays up for ten minutes in My team, shorter than other sports because game two of a doubleheader can start soon after game one. Then the panel looks up your team's next game.

A postponed, canceled or suspended game shows ESPN's word for it and gets no victory splash.
