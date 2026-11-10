---
title: "Football: NFL and college"
description: "What the panel shows for an NFL or college football game: the next-game card, the live board, the red zone, the scoring and turnover splashes, halftime and the final."
sidebar:
  order: 5
---

NFL and college football games share one board. What you see depends on your panel size; this page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Before the game

The next-game card shows both team logos, your team on the left, with each team's abbreviation in its own color and the season records under the logos.

The ticker along the bottom carries:

- the game's name for bowl and playoff games, like "Rose Bowl" or "NFC Wild Card"
- the kickoff day and time in the panel's timezone, like "Sat 3:30 PM" or "Tomorrow 7:15 PM"
- the betting line, the over/under ("O/U") and the TV network, when ESPN has them and **Pre-game odds and TV** is on
- the venue

A game whose kickoff time isn't set yet, like a college game a couple of weeks out, shows the day and "TBD", like "Sat TBD".

A ranked college team shows its rank in front of its name. Before kickoff the ticker starts with the matchup, like "#5 OSU vs #12 MICH", after the game's name when it has one. On a 128x64 board the rank sits in front of the record under the logo, like "#5 9-1", and on a single 64x64 panel in front of the abbreviation, like "#5 OSU". Each goes in only where it fits; see [Ranks and standings](/using/ticker/#ranks-and-standings).

An NFL team can show its playoff seed or its place in its division or conference where its record goes, with **Standing next to pro teams** on the device page.

## During the game

At kickoff the board switches to the live scoreboard:

- **Scores** in large type in the middle.
- **The game clock and quarter** under them, like "12:34 2nd", or "OT" in overtime.
- **Down and distance** under the clock, like "3rd & 4". It turns red in the red zone.
- **Possession**: a gold line under the abbreviation of the team with the ball. The abbreviations stay in team colors.
- **Records** under each logo.
- **Timeout pips** under each logo: three bars per team, lit gold for each timeout left and dim for each one used.
- **The ticker** with the down, distance and where the ball is, like "3rd & 4 at FIU 31", and the last play. Turn either off in the Ticker card on the device page. See [The ticker](/using/ticker/).

When the team with the ball is inside the 20, the ticker says "RED ZONE" before the down and distance. ESPN also marks extra-point tries and the moments after a score as the red zone, and the panel leaves those out, along with halftime.

When ESPN leaves a team's count out, which it does between some plays, the timeout pips keep the last count from the same half instead of dropping to zero. They start at three before kickoff and when the second half begins, including while ESPN still sends the first half's counts before the half's first play. They stay hidden when the panel can't know the count, like when ESPN leaves a count out in overtime, right after a restart mid-game, and on the final scores in Live games.

On a single 64x64 panel, the logos sit side by side with the scores under them. During play the clock takes the abbreviations' place and the gold possession line goes under the score. There is no room for records, timeout pips or the down and distance row, so down and distance is only in the ticker.

## Scoring splashes

When the score changes, the panel fills the screen with a splash for 4 seconds, in the scoring team's color, with the logo of the team that scored and the points it gained:

| Score | Splash | Points |
| --- | --- | --- |
| Touchdown (6 to 8 points at once) | TOUCHDOWN, with fireworks | **+6**, or **+7** with its extra point in the same update |
| Field goal | FIELD GOAL | **+3** |
| Extra point | EXTRA POINT | **+1** |
| 2-point conversion | 2-PT | **+2** |
| Safety | SAFETY | **+2** |
| Any other score | SCORE | The points |

Two points say which they were. A play ESPN calls a safety says "SAFETY", and so do two points in the middle of a drive, when the panel last saw a team with the ball and a down to play. Two points after a touchdown say "2-PT" for the conversion, and so do two points the panel can't tell apart, since a conversion is far more common than a safety.

A touchdown gets fireworks on black with the words in the team's color. The smaller scores flash the screen in the team's color with white words.

:::note[Coming in the next release]
A splash stays up for about 7 seconds. A touchdown and its extra point a read later still show one after the other, with the board between them, and a new splash that comes while one is up takes its place. A touchdown's fireworks go off around the logo and the points, above the words, so the words stay clear of them. See [Splashes](/using/panel-sizes/#splashes).
:::

The logo says whose play it was, so the other side's splash leaves its abbreviation out and says just TOUCHDOWN, in their color. The other side's splashes need **Opponent scores too** in the Celebrations card. The logo is the one already on the board. If it hasn't downloaded yet, the splash shows the team's abbreviation on a badge in its colors instead, so it never waits. See [Splashes](/using/panel-sizes/#splashes) for where each part goes on your panel.

The panel compares each score with the one before, about every 10 seconds, so a touchdown and the extra point that land in the same update show as one TOUCHDOWN.

### Turnovers

Two big plays that aren't scores get a splash too, when ESPN's last play is one:

- **INTERCEPTION**: the team that picked off the pass. On a single 64x64 panel the word breaks over two lines, "INTER-" and "CEPTION".
- **FUMBLE RECOVERY**: the team that took the ball. Only a fumble the other team recovers counts. A fumble the offense falls on never says FUMBLE RECOVERY, even when the offense runs it in for a touchdown, and neither does a muffed punt or kickoff.

Each play counts once. When ESPN fixes the words of a play it already sent, like the yards on a return, it doesn't splash again.

Both flash the screen in the color of the team that took the ball away, with its logo and no points. The other side's need **Opponent scores too**. An interception or a fumble returned for a touchdown shows as TOUCHDOWN.

### Switches, other games and the win

Every splash on this page has its own switch in the Celebrations card, so you can keep touchdowns and turn off extra points, for example. See [Celebrations](/using/device-page/#celebrations).

A big play in another football game can take the board for a minute, with **Switch to big plays in other games** on. The panel checks those games about every 45 seconds, so it goes mostly by the score: 6 to 8 points are a touchdown (with its try, when both land in one check), 3 a field goal and 1 an extra point. Other points between two checks, like a touchdown and a field goal, show as SCORE, under the **Touchdown** switch. An interception or a lost fumble switches the board only when it's still ESPN's latest play at the check. Either team's big play counts. See [Big plays in other games](/using/modes/#big-plays-in-other-games).

When your team wins, a victory splash with fireworks shows your logo and "WINS".

## Halftime and the final

At halftime the clock row says "Halftime" and the ticker shows both teams' records.

The panel goes by ESPN's game status and its plays. ESPN's status sometimes stays at halftime after the second half starts. Once a play from the second half comes in, the clock row shows that play's clock with "3rd" (or just "3rd" when the play has no clock) and the down and distance stay up, instead of "Halftime" staying on the screen. The clock also comes back as soon as ESPN says a quarter has started. At halftime and at the end of the 2nd quarter, the 4th quarter and each overtime period, the down and distance, the possession line and the red zone color go away until the next play, since they belong to the drive that just ended.

At the final the ticker says "Final" with the records. The final stays up for half an hour, then the panel moves on to your team's next game. In Live games the panel moves to another game instead, and in Favorite teams **After the final** sets how long it stays; see [Favorite teams](/using/favorites/).

A postponed, canceled or suspended game shows ESPN's word for it instead of "Final", and gets no victory splash.

The records at halftime and after the final carry a college team's rank in front of its name, like "Final | #5 OSU 10-1 | MICH 8-3". With **Standing next to pro teams** set to **Seed** or **Place**, your NFL team's seed or place takes its record's place there, like "Final | DAL Seed 4 | NYG 6-7".
