---
title: "Hockey: NHL"
description: "What the panel shows for an NHL game, and the same board for the AHL and the ECHL: the period and clock, overtime and shootouts, goal splashes and the playoff series."
sidebar:
  order: 8
---

NHL games get their own board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

AHL and ECHL teams get this same board, with their scores from HockeyTech, the leagues' own stats service, instead of ESPN. See [Hockey: AHL and ECHL](/using/minor-league-hockey/) for what's different.

## Before the game

The next-game card shows both logos, your team on the left, with the records under them. The ticker carries the puck drop day and time in the panel's timezone, the playoff series when there is one, the betting line, over/under and TV channel when ESPN has them and **Pre-game odds and TV** is on, and the arena.

NHL teams have no Up next list on the device page: their season schedules are too large for the panel to read.

## During the game

- **The clock and period** under the scores, like "12:34 2nd".
- **Overtime** reads "OT". In the playoffs, where overtime goes on until someone scores, later periods read "2OT", "3OT" and so on.
- **A shootout**, in the regular season, reads "SO".
- **Intermissions** show ESPN's words, like "End of 1st", and the ticker shows the series, or the records outside the playoffs. The clock comes back as soon as ESPN says the next period has started.
- **The ticker** with the last play. Turn it off with **Last play** in the Ticker card. See [The ticker](/using/ticker/).

The board doesn't show power plays. ESPN's NHL data has no power play field, and working one out from the penalties would be wrong when penalties offset.

On a single 64x64 panel the clock takes the abbreviations' place during play, and there is no room for the records.

## Goal splashes

A goal fills the screen for about 7 seconds with fireworks on black, in the scoring team's color, with the scoring team's logo, **+1** and "GOAL". The fireworks go off around the logo and **+1**, above "GOAL". The logo says whose goal it was, so the other side's splash leaves its abbreviation out. The other side's goals splash unless you turn off **Opponent scores too** in the Celebrations card. The winning goal in a shootout splashes like any other. See [Splashes](/using/panel-sizes/#splashes).

Goal splashes have their own switch in the Celebrations card. See [Celebrations](/using/device-page/#celebrations).

Every goal splashes the same way. ESPN's words for a goal don't say whether it came on a power play or into an empty net, and the number after the scorer's name counts goals this season, not this game, so there is no power play goal, empty net goal or hat trick splash.

### Goal animation

:::note[Coming in the next release]
Before a goal's splash, the panel plays the goal across the whole screen for about 3 seconds, in pixel art, with skaters in the scoring team's colors on a white rink with its blue and red lines and the blue crease: a skater carries the puck in, winds up and slaps it, the puck streaks across the ice over the goalie's stacked pads into the net, the net shakes and the red goal light behind it flashes, and the skater raises his stick. Then a long **GOOOOAAALLL** grows in letter by letter across the top in the team's colors, with a flash at the end, while the goal light keeps flashing. The word stretches to fit: GOOOAL on a single panel, and in letters twice as big on two rows of two; more O's, A's and L's on 128x64 and on three or four panels in a row.

The scoring team always shoots from its own side of the board toward the other side, so a goal by the team on the right goes right to left. A team whose color would vanish on the ice plays in a darker shade of it. Then the splash shows for its usual 7 seconds. A shootout attempt gets no animation, and neither does a goal the panel can't pin on one team, like a goal for each side between two reads. **Play animations** in the Celebrations card turns it off, and **Try it** there plays one now. See [Celebrations](/using/device-page/#celebrations).
:::

When your team wins, a victory splash with fireworks shows your logo and "WINS".

A goal in another hockey game can take the board for a minute, with **Switch to big plays in other games** on, for either team. The panel checks those games about every 45 seconds. See [Big plays in other games](/using/modes/#big-plays-in-other-games).

## The playoff series

In the playoffs the series, like "CAR leads series 2-0", is in the ticker before the game, and takes the records' place during intermissions and at the final. It isn't on the board itself.

## The final

The ticker says "Final", or "Final/OT", "Final/SO" or "Final/2OT", with the series or the records. The final stays up for half an hour in My team, then the panel looks up your team's next game.

A postponed, canceled or suspended game shows ESPN's word for it and gets no victory splash.
