---
title: "Basketball: NBA, WNBA and college"
description: "What the panel shows for an NBA, WNBA or men's college basketball game: the period and clock, the playoff series and the victory splash."
sidebar:
  order: 7
---

NBA, WNBA and men's college basketball games share one board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Before the game

The next-game card shows both logos, your team on the left, with the records under them. The ticker carries the tip-off day and time in the panel's timezone, the playoff series when there is one, the betting line, over/under and TV channel when ESPN has them and **Pre-game odds and TV** is on, and the arena.

Basketball teams have no Up next list on the device page: their season schedules are too large for the panel to read.

:::note[Coming in the next release]
A ranked men's college basketball team shows its rank in front of its name. Before tip-off the ticker starts with the matchup, like "#2 CONN vs #1 MICH", and the records after the final read "#2 CONN 34-6 | #1 MICH 37-3". On a 128x64 board the rank sits in front of the record under the logo, and on a single 64x64 panel in front of the abbreviation before and after the game, each only where it fits. See [Ranks and standings](/using/ticker/#ranks-and-standings).

An NBA or WNBA team can show its playoff seed or its place in its division or conference where its record goes, with **Standing next to pro teams** on the device page.
:::

## During the game

- **The clock and period** under the scores, like "1:04 4th", or "45.3 4th" in the last minute, when ESPN drops the minutes. Overtime reads "OT", then "2OT" and so on. College games play halves, so the clock reads "12:34 1st" or "12:34 2nd".
- **Breaks**: "Halftime", or "End" and the period, like "End 3rd".
- **The playoff series** on the row under the clock, like "NY lead 2-1" or "Series 2-2", when the game is a playoff game.
- **The ticker** with the last play. During a break it shows the series, or the records outside the playoffs.

Basketball has no game situation line, so there is no ticker switch for one. **Last play** in the Ticker card turns the last play on or off. See [The ticker](/using/ticker/).

On a single 64x64 panel the clock takes the abbreviations' place during play. There is no room for the series row or the records.

## Splashes

Basketball only celebrates the win. A game has around a hundred scoring plays, so splashing each basket would keep the screen covered. When your team wins, a victory splash with fireworks says your abbreviation and "WINS". **Opponent scores too** makes no difference for basketball.

:::note[Coming in the next release]
Baskets still don't splash, but four moments do, each with its own switch in the Celebrations card:

- **LEAD CHANGE**: the team that was behind goes ahead in the last 2 minutes of the 4th quarter (of the 2nd half in college) or in overtime. Breaking a tie doesn't count, since the team ahead before the tie may just be ahead again.
- **BUZZER BEATER**, with fireworks: a basket in the last second of a period that ties the game or takes the lead. The panel reads the game about every 10 seconds, so it counts a basket when the clock stopped on it with a second or less left, or when the clock ran out and the update before showed a second or less. A basket from further out that beats the horn may get no splash. When ESPN marks the game final in the same update, a game winner at the buzzer gets this instead of the victory splash. When ESPN marks it final an update later, the buzzer beater shows first and the victory splash follows.
- **OVERTIME**: when overtime starts, and each overtime after that, even when the first update in overtime already has a basket in it.
- **FINAL**: when the game ends and your team didn't win. A win still gets the victory splash, and in Live games every final does.

The other side's lead changes and buzzer beaters put its abbreviation first, like "MIN LEAD CHANGE", and need **Opponent scores too**. See [Celebrations](/using/device-page/#celebrations).
:::

## The final

The ticker says "Final", or "Final/OT" after overtime, with the series or the records. The final stays up for half an hour in My team, then the panel looks up your team's next game.

A postponed, canceled or suspended game shows ESPN's word for it and gets no victory splash.
