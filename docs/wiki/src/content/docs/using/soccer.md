---
title: "Soccer: MLS and the Premier League"
description: "What the panel shows for an MLS or Premier League match: the minute and stoppage time, the latest goal, red cards, goal, penalty and red card splashes, cup games and the next three fixtures."
sidebar:
  order: 9
---

MLS and Premier League matches share one board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Cup games

The panel follows your club into its cup competitions too: Premier League clubs into the Champions League, Europa League and Conference League, and MLS clubs into the CONCACAF Champions Cup. Whichever game comes first, league or cup, is the one on the board. A cup game's ticker names its stage, like "UEFA Champions League, League Phase".

## Before the match

The next-game card shows both logos, your club on the left, with each club's record under its logo as wins, draws and losses. A record too long to fit there shows as points instead, like "52 pts". The ticker carries:

- the kickoff day and time in the panel's timezone
- the cup stage, for a cup game
- each club's recent form
- the three-way odds, like "ARS -275, draw +425, LEE +700", when ESPN has them and **Pre-game odds and TV** is on
- the stadium

## During the match

- **The minute** in the clock row, like "67'". Stoppage time adds to the half's last minute, like "45'+3'".
- **Breaks**: "HT" at half time. Extra time reads "ET" and the minute, like "ET 105'", and "ET HT" at its break. A penalty shootout reads "PENS" with the score, like "PENS 3-2".
- **The latest goal** on the row under the clock, with the minute and the scorer, like "52' Saka". An own goal is marked "OG" and a penalty "(P)".
- **Red cards** as small red marks under the carded club's logo, one per red card, up to three.
- **The ticker** with the match stats and the latest goal or card. See below.

On a single 64x64 panel there is no room for the latest goal row, the red card marks or the records. The match stats in the ticker carry the goals and cards.

### Match stats in the ticker

With **Match stats** on in the Ticker card, the ticker lists:

- the goals for each side with the minute and scorer, like "Goals ARS 58' Guimaraes, 90'+7' Saka (P); SUN 12' Isidor"
- yellow and red cards for each side, like "2Y 1R"
- possession and shots, with shots on target in brackets

**Last play** adds the latest goal or card. At half time the ticker shows the goals, cards, stats and records, whatever the switches say. See [The ticker](/using/ticker/).

## Goal and card splashes

A goal fills the screen for about 7 seconds with fireworks on black, in the scoring club's color, with the club's logo, **+1** and "GOAL". The fireworks go off around the logo and **+1**, above "GOAL". The other side's goals splash unless you turn off **Opponent scores too** in the Celebrations card. Two more splashes, up for about 7 seconds too:

- **PENALTY GOAL**, with fireworks: a goal from the penalty spot, from ESPN's match details, with the logo and **+1**. Kicks in a shootout don't count.
- **RED CARD**: a red card for either side, on a red screen, with the logo of the side that got it and no points. It shows whichever side gets the card, whatever **Opponent scores too** says.

The logo says which club it was, so the words leave its abbreviation out. See [Splashes](/using/panel-sizes/#splashes).

Goals, penalty goals and red cards each have a switch in the Celebrations card. With **Penalty goal** off, a penalty shows as a goal. See [Celebrations](/using/device-page/#celebrations).

### Goal animation

:::note[Coming in the next release]
Before a goal's or a penalty goal's splash, the panel plays the goal across the whole screen for about 3 seconds, in pixel art, with players in the scoring club's colors under the stadium lights: the striker runs onto the ball and shoots, the ball spins across the panel past the diving keeper into the top corner, and the net bulges where it hits while the striker slides on his knees. Then a long **GOOOOAAALLL** grows in letter by letter across the top in the club's colors, with a flash at the end. The word stretches to fit: GOOOAL on a single panel, and more O's, A's and L's on wider setups, up to 25 letters on four panels in a row.

The scoring club always shoots toward its own side of the board, so a goal by the club on the right goes right to left. Then the splash shows for its usual 7 seconds. An own goal plays for the club it counts for. Kicks in a shootout get no animation, and neither does a goal the panel can't pin on one club, like two goals, one for each side, between two reads. **Play animations** in the Celebrations card turns it off, and **Try it** there plays one now. See [Celebrations](/using/device-page/#celebrations).
:::

At full time a win gets a victory splash with fireworks, your logo and "WINS". A draw gets no splash. When a shootout decides a level game, winning the shootout counts as the win.

A goal, a penalty goal or a red card in another match can take the board for a minute, with **Switch to big plays in other games** on, for either team. The panel checks those matches about every 45 seconds, from the league's day of matches, so a favorite's cup match, like one in the Champions League, isn't watched. See [Big plays in other games](/using/modes/#big-plays-in-other-games).

## Full time

The clock row says "FT", "AET" after extra time, or "PENS" after a shootout. The ticker says "Full time", "After extra time" or who won on penalties, like "PSG win 4-3 on penalties", then the cup stage, the goals, any red cards and both records. The final stays up for half an hour in My team, then the panel looks up the next match.

A postponed or canceled match shows ESPN's word for it and gets no victory splash.

## The next three games

On the device page, the **Up next** list shows your club's next three games after the one on the board, with the opponent, the day and time and the TV channel. It comes from the club's league schedule, so a cup game the panel is about to follow may not be in it.
