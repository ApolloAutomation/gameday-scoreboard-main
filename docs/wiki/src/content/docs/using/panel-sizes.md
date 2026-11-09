---
title: "Panel sizes and layouts"
description: "The three panel arrangements the firmware drives, what each layout shows, and how to change it on the device page."
sidebar:
  order: 11
---

One firmware drives three arrangements of HUB75 LED panels. The panel size is a setting on the device page, not a different install.

| Arrangement | Layout | Pick on the device page |
| --- | --- | --- |
| One 128x64 panel | Wide | **One 128x64 panel** |
| Two 64x64 panels side by side | Wide | **One 128x64 panel** (the picture says "or two 64x64 side by side") |
| One 64x64 panel | Single | **One 64x64 panel** |

The Game Day Scoreboard ships with one 128x64 panel, and its firmware starts on the wide layout. Built your own? See [Panels](/diy/panels/).

:::note[Coming in the next release]
Three more arrangements, for 64x64 panels:

| Arrangement | Layout | Pick on the device page |
| --- | --- | --- |
| Three 64x64 panels side by side | Wide, 192x64 | **Three wide** |
| Four 64x64 panels side by side | Wide, 256x64 | **Four wide** |
| Four 64x64 panels in two rows of two | Two rows, 128x128 | **Two rows of two** |

With three or four panels, Bluetooth setup is off, so the panel sets up Wi-Fi through its hotspot only: scan the QR code on its setup screen with your phone's camera. Power them from a supply rated for all of them. See [Panels and power](/diy/panels/).
:::

## What each layout shows

### Wide: 128x64

The logos sit at the far edges with the abbreviations and big scores between them. Under the scores is the game clock, and under that the situation row: down and distance in football, red in the red zone; the count, a diamond of the runners and the outs in baseball; the latest goal in soccer; the playoff series in basketball. The records sit under the logos, with football's timeout pips or soccer's red card marks under them. The ticker runs along the bottom.

:::note[Coming in the next release]
A ranked college team's rank goes in front of its record under the logo, like "#5 9-1", when the two fit, and keeps clear of the game clock between them. A pro team can show its seed or place there instead of its record. See [Ranks and standings](/using/ticker/#ranks-and-standings).
:::

On the start-up screen, the QR code sits on the left half with the words beside it. It joins the panel's hotspot during setup, once the hotspot is up, and opens the device page once the panel is on Wi-Fi. Without a QR code up, the words use the whole width.

:::note[Coming in the next release]
**Three or four wide (192x64, 256x64).** The wide layout spreads across the whole width: the logos at the far ends, the abbreviations and big scores between them, the game clock and the situation row in the middle, and the ticker along the whole bottom. A golf or racing leaderboard sits in the middle. The game clock is far from the records under the logos, so a rank or a standing there always has its 7 characters. A splash's words are centered across the whole width, and a touchdown's or a win's fireworks go off in the middle behind them. On the start-up screen, the words take everything to the right of the QR code.

**Two rows of two (128x128).** The top half is the 128x64 board, the same as on one 128x64 panel. The bottom half shows the time and the date, and under them the ticker in bigger letters. Under the off-season clock or the idle display's clock, which already show the time, the bottom half leaves the time out: it shows the ticker, with the date above it when the clock has your next game under the time instead of the date. A splash covers all four panels, with its words and fireworks across the middle. The start-up and setup screens sit in the middle of the four panels.
:::

### Single: 64x64

The two logos sit side by side at the top, with the abbreviations and scores stacked under them. During play the game clock takes the abbreviations' place, and the gold possession line in football goes under the score. Baseball's diamond sits between the two scores with the outs as three dots under it.

A single panel has no room for the records, the timeout pips, the red card marks or the situation row, so down and distance, the count and the soccer goals are in the ticker only, when their switches are on. See [The ticker](/using/ticker/).

:::note[Coming in the next release]
Before and after the game, a ranked college team's rank goes in front of its abbreviation, like "#5 OSU", when the two fit in the 6 characters there. See [Ranks and standings](/using/ticker/#ranks-and-standings).
:::

The off-season clock shows the date under the time, and the next game goes in the ticker. On the start-up screen, the QR code and the words take turns.

## Changing the layout

1. Open the device page and find the **Setup** card.
2. Under **Matrix**, click the picture that matches your panel.
3. The page says **Restarting as** and the size. The panel restarts with the new layout a second later, and the page reconnects when it's back.

Your team and settings stay as they were. A [factory reset](/using/buttons-and-resets/) puts the layout back to the one the firmware starts on: 128x64 for the Game Day Scoreboard and the two-panel DIY build, 64x64 for the one-panel DIY build.

:::note[Coming in the next release]
The page asks before it restarts the panel, and says **Restarting as** and the size once you agree. For **Two rows of two**, a **How the rows are mounted** choice appears under the pictures once the panel is back: **Bottom row upside down** (folded under, where a new panel starts), **All upright** or **Top row upside down**. Changing it restarts the panel too. See [Panels and power](/diy/panels/#chain-two-64x64-panels) for the wiring.
:::

## Wrong size?

If the layout doesn't match the panel, the picture looks wrong:

- **The picture shows twice, side by side** (two QR codes during setup, for example): the panel is set to one 64x64 panel but is a 128x64 display. Pick **One 128x64 panel**.
- **Half the board is blank, or only part of it shows**: the panel is set to 128x64 but is a single 64x64 panel. Pick **One 64x64 panel**.

:::note[Coming in the next release]
- **The picture repeats across the panels, or some panels stay dark**: the count doesn't match. Pick the picture with as many panels as you have.
- **The parts of a three or four wide board show in the wrong order**: put the panels in the reverse order. Seen from the front, the panel with the board goes at the right-hand end.
- **Part of a two rows of two board is upside down**: the wiring choice doesn't match how the rows are mounted.

| You picked | What you see | Pick instead |
| --- | --- | --- |
| **Bottom row upside down** | The bottom half upside down | **All upright** |
| **Bottom row upside down** | Both halves upside down | **Top row upside down** |
| **All upright** | The bottom half upside down | **Bottom row upside down** |
| **All upright** | The top half upside down | **Top row upside down** |
| **Top row upside down** | Both halves upside down | **Bottom row upside down** |
| **Top row upside down** | The top half upside down | **All upright** |

If the bottom half's left and right sides are swapped, or each bottom panel shows its own part upside down, the bottom row is wired the other way round and no choice fixes it. See [Panels and power](/diy/panels/#chain-two-64x64-panels) for where each panel goes.
:::

More fixes are in [Troubleshooting](/help/troubleshooting/).
