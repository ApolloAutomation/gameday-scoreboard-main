---
title: "Football: NFL and college"
description: "What the panel shows for an NFL or college football game: the next-game card, the live board, the scoring splashes, halftime and the final."
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

## During the game

At kickoff the board switches to the live scoreboard:

- **Scores** in large type in the middle.
- **The game clock and quarter** under them, like "12:34 2nd", or "OT" in overtime.
- **Down and distance** under the clock, like "3rd & 4". It turns red in the red zone.
- **Possession**: a gold line under the abbreviation of the team with the ball. The abbreviations stay in team colors.
- **Records** under each logo.
- **Timeout pips** under each logo: three bars per team, lit gold for each timeout left and dim for each one used.
- **The ticker** with the down, distance and where the ball is, like "3rd & 4 at FIU 31", and the last play. Turn either off in the Ticker card on the device page. See [The ticker](/using/ticker/).

:::note[Coming in the next release]
When the team with the ball is inside the 20, the ticker says "RED ZONE" before the down and distance. ESPN also marks extra-point tries and the moments after a score as the red zone, and the panel leaves those out, along with halftime.
:::

:::note[Coming in the next release]
When ESPN leaves a team's count out, which it does between some plays, the timeout pips keep the last count from the same half instead of dropping to zero. They start at three before kickoff and when the second half begins, including while ESPN still sends the first half's counts before the half's first play. They stay hidden when the panel can't know the count, like when ESPN leaves a count out in overtime, right after a restart mid-game, and on the final scores in Live games.
:::

On a single 64x64 panel, the logos sit side by side with the scores under them. During play the clock takes the abbreviations' place and the gold possession line goes under the score. There is no room for records, timeout pips or the down and distance row, so down and distance is only in the ticker.

## Scoring splashes

When the score changes, the panel fills the screen with a splash for 4 seconds, in the scoring team's color. Your team's splashes say:

| Score | Splash |
| --- | --- |
| Touchdown (6 to 8 points at once) | TOUCHDOWN, with fireworks |
| Field goal | FIELD GOAL |
| Extra point | EXTRA POINT |
| Two points (a conversion or a safety) | 2-POINT |
| Any other score | SCORE |

A touchdown gets fireworks on black with the words in your team's color. The smaller scores flash the screen in your team's color with white words.

The other side's scores show the same way in their color with their abbreviation in front, like "PHI TOUCHDOWN", unless you turn off **Opponent scores too** in the Celebrations card.

The panel compares each score with the one before, about every 10 seconds, so a touchdown and the extra point that land in the same update show as one TOUCHDOWN.

When your team wins, a victory splash with fireworks says your abbreviation and "WINS".

## Halftime and the final

At halftime the clock row says "Halftime" and the ticker shows both teams' records.

:::note[Coming in the next release]
The panel goes by ESPN's game status and its plays. ESPN's status sometimes stays at halftime after the second half starts. Once a play from the second half comes in, the clock row shows that play's clock with "3rd" (or just "3rd" when the play has no clock) and the down and distance stay up, instead of "Halftime" staying on the screen. The clock also comes back as soon as ESPN says a quarter has started. At halftime and at the end of the 2nd quarter, the 4th quarter and each overtime period, the down and distance, the possession line and the red zone color go away until the next play, since they belong to the drive that just ended.
:::

At the final the ticker says "Final" with the records. The final stays up for half an hour, then the panel moves on to your team's next game. In Live games the panel moves to another game instead, and in Favorite teams **After the final** sets how long it stays; see [Favorite teams](/using/favorites/).

A postponed, canceled or suspended game shows ESPN's word for it instead of "Final", and gets no victory splash.
