---
title: "Golf: PGA Tour and LPGA Tour"
description: "Follow the PGA Tour or the LPGA Tour like a team: this week's tournament with the top of its leaderboard, the next event between tournaments, and a clock between seasons."
sidebar:
  order: 9.5
---

You follow a golf tour the way you follow a team. Pick the **PGA Tour** or the **LPGA Tour**, and the panel shows that tour's tournament this week: its name, the round, and the top of the leaderboard. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/). The names and scores in its examples show the format, not real results.

## Following a tour

On the device page, tap **Change team** and open the **Golf** tab, which holds both tours. Tap **PGA Tour** or **LPGA Tour**. Search finds them too, by their names or by "golf". In Home Assistant, the **Team** entity has **Golf: PGA Tour** and **Golf: LPGA Tour** near the end of its list, after the NHL teams.

The tour is your team in **My team**. In **Live games**, when nothing is live, the panel shows your tour the way it would show your team, with "No live NFL games, showing PGA" at the front of the ticker. See [Show: My team, Favorite teams, Live games](/using/modes/#when-nothing-is-live).

## Before a tournament

The board shows the tournament's name and its days, like "The RSM Classic" over "Nov 19-22". A long name is shortened to fit: a sponsor's name goes first, then a place like "in Hawaii", then a last word like "Championship", which becomes "Champ" when only one short word would be left ("PGA Champ"). On a single panel a word too long for its line is shortened, like "Int'l" for "International". The ticker carries:

- the days
- the course and city
- the purse and the TV channel, when **Pre-game odds and TV** is on
- last year's winner, as "Defending champion"

The leaderboard takes over once ESPN says the first round has started.

## During a round

The top row has the round, like "Round 3", in green. On a 128x64 display the tournament's name, shortened to fit, is on the left of it. Under it, six rows of the leaderboard:

- **Place**: a "T" marks a tie, like "T4". From 10th place on there's no room for the "T".
- **Last name**: up to 7 letters on a single panel and 11 on a 128x64 display, cut to fit. Nothing on the board scrolls but the ticker. The ticker, the device page and Home Assistant have the whole name.
- **Score to par**: red under par, white at even ("E"), blue over par.
- **Holes played**, on a 128x64 display only: the number of holes so far this round, "F" once a player has finished it, or the tee time of a player who hasn't started, like "8:27p", in the panel's timezone.

The ticker has the tournament, the round and the top 10, like "T1 Iwai -12 thru 7, T1 Hataoka -12 thru 4, 3 Hara -11 thru 5", then the TV channel when **Pre-game odds and TV** is on.

## Between rounds, suspensions and playoffs

Between rounds the leaderboard stays up and the top row says "After R2". The holes column shows the next round's tee times, and the ticker says when the leaders tee off, like "Leaders tee off 1:50a".

When play stops for weather or darkness, the top row says "R2 suspended" or "R2 delayed" in amber, and the leaderboard stays as it was. A playoff for the win shows "Playoff".

## The final

The top row says "Final" in gold and the winner's name turns gold. When the panel sees the tournament end, the winner's name fills the screen with fireworks for about 7 seconds, like "JAMES WINS!". On a single 64x64 panel every burst goes off inside the panel, so none is cut off at its edge. On a single panel a name longer than 10 letters is cut to fit its line, like "YELLAMARAJ WINS!". The ticker says who won and at what score, like "James wins at -20", then the next four places and the tour's next event with its days.

The final stays up until ESPN moves on to the tour's next event, usually a day or two later. Then the board shows that event's name and days. If the panel can't read the tour's schedule after a final, the final stays up and the panel tries again within minutes, rather than saying there's no event to come.

A canceled or postponed tournament shows its name and "Canceled" or "Postponed".

## Between seasons

An event more than 10 days away shows as the off-season clock, in green. So does a final more than 10 days old that ESPN still lists, unless the tour's next event starts within 10 days: then the board shows that event's name and days. On a 128x64 display the next event and its days sit under the time, like "Next: Hilton Grand" over "Feb 4-7", and on a single panel the date does. The ticker says "Next:" with the event's full name and days, or "No upcoming PGA Tour event" when ESPN has nothing on the schedule yet.

If ESPN lists no event for the tour at all, the board says "No upcoming event". See [Brightness, panel on and off, and the clock](/using/brightness-and-clock/).

## How often it updates

Every minute while a round is being played, every 5 minutes between rounds and in the 12 hours before a tournament starts, every 30 minutes the rest of the season, and every 6 hours between seasons. **Refresh scores** on the device page reads it again right away. Each read is small, about 19 KB during a tournament.

A tournament's days are worked out in US time. In Asia and Australia they can show one day late, as a college football game with no kickoff time yet does.

## On the device page and in Home Assistant

The board on the device page shows the leaderboard too: the tournament's name, the round, and up to 10 rows with the place, the name, the score to par and the holes played. Before a tournament it shows the days, and after a final the next event.

Home Assistant's **Game Status** reads like "TOTO Japan Classic | Round 3 | Leader: Iwai -12", "World Wide Technology Championship | Final | James wins at -20" or "The RSM Classic | Nov 19-22". It changes when the leader or their score changes, at a new round and at the final, not on every hole. **Last Play** stays empty for golf. See [Home Assistant](/using/home-assistant/).

## Not there yet

This first version follows a tour, not a player. Not there yet:

- Following one golfer through a tournament.
- A tour as a favorite. The **Golf** tab is hidden while you add a favorite. See [Favorite teams](/using/favorites/).
- Golf in **Live games**. Its league buttons and the **On now** tab leave golf out.
- Other tours, like the DP World Tour, the PGA Tour Champions, the Korn Ferry Tour or LIV Golf.
- Team events and match play. The board shows them, but names like "Novak/Griffin" are cut short and the place can show as "-".
