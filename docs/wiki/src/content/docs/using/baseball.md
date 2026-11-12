---
title: "Baseball: MLB"
description: "What the panel shows for an MLB game: the inning, the count, outs, the runners on a diamond, run, home run, grand slam and walk-off splashes, and the final."
sidebar:
  order: 6
---

MLB games get their own board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

Minor league teams, at Triple-A, Double-A, High-A and Single-A, get this same board, with their scores from MLB instead of ESPN. See [Baseball: minor leagues](/using/minor-league-baseball/) for what's different.

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

## Runs, home runs, grand slams and walk-offs

When the score changes, the panel fills the screen for about 7 seconds in the scoring team's color, with the logo of the team that scored and the runs it gained:

| Score | Splash | Runs shown |
| --- | --- | --- |
| One run | RUN for your team, SCORES for the other side | **+1** |
| Two or more runs in one update | The number and RUNS, like "2 RUNS" | The number, like **+2** |
| Home run | HOME RUN, with fireworks | The runs, like **+2 RUNS** |
| Grand slam | GRAND SLAM, with fireworks | **+4 RUNS** |
| Walk-off | WALK-OFF, with fireworks | The runs, like **+1 RUN** |

- **GRAND SLAM**: a home run that ESPN says scored four runs, or one ESPN's play calls a grand slam. Runs from an earlier play in the same update don't make a home run a grand slam.
- **WALK-OFF**: the home team goes ahead in the bottom of the 9th inning or later, which ends the game. When ESPN marks the game final in the same update, the walk-off takes the victory splash's place. When ESPN marks it final an update later, the walk-off shows first and the victory splash follows. If the panel missed every update from the 8th inning to the final, it can't tell where the lead came from, so the final gets only its usual splash.

The logo says whose run it was, so the other side's splash leaves its abbreviation out. A home run counts its own runs and a grand slam is always **+4 RUNS**, even when another run scored earlier in the same update. On a single 64x64 panel, and at the right end of three or four panels in a row, there's room for the number only, like **+2**. See [Splashes](/using/panel-sizes/#splashes).

A home run, a grand slam and a walk-off get fireworks on black with the words in the team's color, the fireworks around the logo and the runs, above the words. Other runs flash the screen in the team's color. The other side's splashes need **Opponent scores too** on.

:::note[Coming in the next release]
Before a home run's, a grand slam's or a home run walk-off's splash, the panel plays the home run across the whole screen for about 3.5 seconds, in pixel art with players in the team's colors: the pitch, the swing, and the ball flying over the outfield wall while the outfielder leaps for it and misses. The batter flips his bat and starts his trot. When ESPN says how far it went, the ball lands just past the wall, in the stands, or leaves the screen for the longest ones, and the distance shows at the top, like **401 FT**, with where it went, like **RIGHT CENTER**, on three or four panels in a row. Then the splash shows for its usual 7 seconds. A home run told from the score alone, in another game, plays without a distance. **Play animations** in the Celebrations card turns them off. See [Celebrations](/using/device-page/#celebrations).
:::

Runs, home runs, grand slams and walk-offs each have a switch in the Celebrations card. With **Grand slam** off, a grand slam shows as a home run, and with **Home run** off, as runs. See [Celebrations](/using/device-page/#celebrations).

When your team wins, a victory splash with fireworks shows your logo and "WINS".

A minor league game gets runs and walk-offs only. MLB's minor league scores don't say how a run scored, so a home run or a grand slam there shows as runs, under the **Runs** switch. In the 7-inning games of a minor league doubleheader, a walk-off counts from the bottom of the 7th. See [Baseball: minor leagues](/using/minor-league-baseball/).

A big play in another baseball game can take the board for a minute, with **Switch to big plays in other games** on. The panel checks those games about every 45 seconds, and by then ESPN's last play is often the next batter's, so a home run is also told from the score and the bases: the batting team scored its runners plus one, the bases are now empty and the outs haven't changed. With the bases loaded before, that's a grand slam. Other runs show as runs, so turn off **Runs** if a switch for every run is too much. A walk-off counts when the panel saw the game in the 9th inning or later. See [Big plays in other games](/using/modes/#big-plays-in-other-games).

## The final

The ticker says "Final", or "Final/10" after extra innings, with each team's hits and errors and the series or the records. The final stays up for ten minutes in My team, shorter than other sports because game two of a doubleheader can start soon after game one. Then the panel looks up your team's next game.

A postponed, canceled or suspended game shows ESPN's word for it and gets no victory splash.
