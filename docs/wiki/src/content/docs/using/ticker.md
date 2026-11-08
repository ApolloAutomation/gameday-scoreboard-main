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

## Ranks and standings

:::note[Coming in the next release]
**College teams.** In college football and men's college basketball, a ranked team gets its rank in front of its abbreviation, like "#5 OSU". The rank is ESPN's: it follows the AP poll, then the College Football Playoff ranking once that's out. Unranked teams have none.

- Before the game the ticker starts with the matchup and the ranks, like "#5 OSU vs #12 MICH" or "FSU vs #19 SMU".
- After the final, and at halftime and other breaks, the records read "#5 OSU 9-1 | #12 MICH 8-2".
- On a 128x64 board the rank goes in front of the record under the logo, like "#5 9-1". That spot holds 7 characters, so when the two don't fit, like "#12 10-1", the record stays and the rank is left out.
- On a single 64x64 panel the rank goes in front of the abbreviation, like "#5 OSU", before and after the game. That spot holds 6 characters, so "#12 OSU" or "#5 MICH" shows just the abbreviation. During play the clock has that row.
- The device page shows the ranks on its board, in the Favorite teams list and in **On now**.

**Pro teams.** **Standing next to pro teams**, in the Panel card on the device page, picks what your team shows where its record goes, under its logo on a 128x64 board, in the ticker and on the device page:

| Choice | Shows |
| --- | --- |
| **Record** | The season record, like "10-3". This is where a new panel starts. |
| **Seed** | ESPN's playoff seed, like "Seed 4". |
| **Place** | The team's place in its division, conference or league, cut short: "1st in NFC East" is "1st NFCE", "2nd in Pacific Division" is "2nd Pac", "2nd in English Premier League" is "2nd EPL". |

It covers the team you follow in My team, or the favorite on the board in Favorite teams. The other team keeps its record, and in Live games and the finals rotation every team shows its record. When ESPN has no seed or place for your team, like a soccer club's seed, the record shows. Under the logo there's room for 7 characters, so a longer place shows just its first part, like "1st". The panel reads the seed and place from ESPN with the team's schedule, every 6 hours.
:::

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

:::note[Coming in the next release]
**IP** and the panel's IP address, then its `.local` name, like **IP 192.168.1.50 | gameday-2f6a70.local**: the panel just joined Wi-Fi. The line leads the ticker for 30 seconds once the QR code and the address go, or straight away when a game is live, with **Show how to connect when the panel starts** on. Open either address in a browser on the same Wi-Fi to get to the device page. Home Assistant's Game Status leaves it out.
:::

For the messages that take over the whole board, see [Panel messages](/help/panel-messages/).

## Ticker speed

**Ticker speed** in the Panel card sets how fast the ticker scrolls, from 1 (slowest) to 10 (fastest). 1 moves 8 pixels a second, about one and a half letters, and 10 moves 32, about six letters. Each step is the same amount faster than the one below it, so every setting stays readable. A new panel starts at 5, about three letters a second. The speed stays the same whatever the length of the line, so a long last play scrolls at the same pace as a short one.

A panel keeps its setting through an update. Before version 2026.10.2 every setting scrolled faster, so after updating from an older version the ticker looks slower at the same number: raise it if you'd like it quicker.
