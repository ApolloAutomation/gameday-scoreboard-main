---
title: "The ticker"
description: "The scrolling line along the bottom of the panel: what it shows before, during and after a game, the switches that control it, and its speed."
sidebar:
  order: 10
---

The ticker is the line that scrolls along the bottom of the panel. It carries what doesn't fit on the board: the kickoff time and odds before a game, the situation and the last play during it, and the result after.

## What it shows

| When | What scrolls |
| --- | --- |
| Before the game | The kickoff day and time in the panel's timezone, the odds and TV channel, the venue, and each sport's extras: a bowl or playoff name in football, the probable pitchers in baseball, the playoff series in basketball and hockey, form and the cup stage in soccer. |
| During the game | The game situation and the last play, each with its own switch. |
| At a break | Halftime in football, the hits and errors between half innings in baseball, the series or records in basketball and hockey, and the goals, cards and stats at half time in soccer. |
| After the final | "Final" with the records, or the sport's result line, like hits and errors in baseball or the goals in soccer. |

A golf tour's ticker has the tournament's days and course before it, the round and the top 10 during it, and the winner and the next event after it. A racing series' ticker has the next session and when it starts, then the track, before a race weekend; the session and the top 10 with their gaps during it, and what's next between sessions; and the winner and the next race after it.

Each sport's page has the details: [Football](/using/football/), [Baseball](/using/baseball/), [Basketball](/using/basketball/), [Hockey](/using/hockey/), [Soccer](/using/soccer/), [Golf](/using/golf/) and [Racing](/using/racing/).

## The switches

The Ticker card on the device page has a switch for each part. All of them start on.

| Switch | What it adds |
| --- | --- |
| **Down and distance** | Football: the down, yards to go and where the ball is, like "3rd & 4 at FIU 31". In the next release it starts with "RED ZONE" when the team with the ball is inside the 20. |
| **Count and at-bat** | Baseball: the count, outs, pitcher and batter. |
| **Match stats** | Soccer: goals, cards, possession and shots. |
| **Last play** | ESPN's latest play in every sport. In soccer, the latest goal or card. A very long play is cut short. |
| **Pre-game odds and TV** | The line, over/under and TV channel before the start, in every league, when ESPN has them. For a golf tour, the purse before a tournament and the TV channel during it. For a racing series, the TV channel, before and during a race weekend. |

These three, Down and distance, Count and at-bat and Match stats, are the game situation for their sport. Hockey, basketball, golf and racing have no situation line, so they have no switch of their own, and golf and racing have no last play either. The card only shows the switches for sports your panel follows, from your team, your favorites and the leagues you picked for Live games, so turning off football's line leaves baseball's count alone.

With every switch off during play, the ticker shows ESPN's short status for the game, like the clock.

The game clock always has its own row on the board, so it never goes in the ticker.

## Messages in the ticker

The ticker also says when something needs your attention:

- **Pick a team at** and the panel's IP address: the panel has never had a team, mode or favorite picked. It goes away once you pick one, on the device page or from Home Assistant. The IP address is the device page's: open it in a browser on the same Wi-Fi. It's there for phones that can't open `.local` names. See [Pick your team](/get-started/pick-your-team/).
- **no update for** and a number of minutes, or **no update yet**: the panel has missed three reads from ESPN in a row. The score may be old. It clears by itself when a read comes through.
- **Favorites: add teams at** and the panel's IP address, or **Favorites: add teams on the device page** while the panel has no address: the panel is in Favorite teams with no favorites.
- **No live games, showing** and your team, like "No live NFL games, showing DAL": Live games found nothing on, so it shows your team until a game starts. See [Show: My team, Favorite teams, Live games](/using/modes/#when-nothing-is-live).
- **Too warm, dimming** in orange, or **Check power supply** in red: the panel is holding its brightness down because it's too warm or its power supply is too weak. The line covers the ticker for 15 seconds every two minutes, until the panel has cooled and is back at your brightness, or until you fix the power and press **Clear** on the device page. See [Brightness, panel on and off, and the clock](/using/brightness-and-clock/#heat-and-power-protection).

For the messages that take over the whole board, see [Panel messages](/help/panel-messages/).

## Ticker speed

**Ticker speed** in the Panel card sets how fast the ticker scrolls, from 1 (slowest) to 10 (fastest). 1 moves 8 pixels a second, about one and a half letters, and 10 moves 32, about six letters. Each step is the same amount faster than the one below it, so every setting stays readable. A new panel starts at 5, about three letters a second. The speed stays the same whatever the length of the line, so a long last play scrolls at the same pace as a short one.

A panel keeps its setting through an update. Before version 2026.10.2 every setting scrolled faster, so after updating from an older version the ticker looks slower at the same number: raise it if you'd like it quicker.
