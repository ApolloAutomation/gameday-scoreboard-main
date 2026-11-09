---
title: "Panels and power"
description: "Which HUB75 panels the Game Day Scoreboard firmware drives, how to chain two, and how to power them."
sidebar:
  order: 3
---

The firmware is made for the common indoor HUB75 panels with an E pin (1/32 scan). It drives one 64x64 panel, or a 128x64 display: one 128x64 panel or two 64x64 panels side by side. Other sizes, like 64x32, aren't supported.

## What the firmware expects

The display settings live in `firmware/gameday-common.yaml` and `firmware/controllers/moonhub75.yaml`:

| Setting | Value |
| --- | --- |
| Panel size | 64x64 pixels each |
| Chain | one row, one or two panels wide (`HORIZONTAL`) |
| Scan wiring | `STANDARD_TWO_SCAN` |
| Clock | 20 MHz |
| Color depth | 8 bits |

Pixel pitch isn't part of these settings, so a 64x64 panel is driven the same way whatever its pitch. The firmware drives a 128x64 panel the same way as two 64x64 panels in a row.

## One panel or two

| You have | Pick | What the board shows |
| --- | --- | --- |
| One 64x64 panel | One 64x64 panel | Logos side by side, stacked scores |
| One 128x64 panel | One 128x64 panel | Logos at the edges, big scores, records and timeout pips in the middle |
| Two 64x64 panels side by side | One 128x64 panel (the same choice) | Same as one 128x64 panel |

One firmware drives both layouts. You pick the layout in the [DIY installer](/diy/install/) before you install, and you can change it later on the device page. See [Panel sizes](/using/panel-sizes/).

:::note[Coming in the next release]
The firmware also drives three or four 64x64 panels in a row (192x64 or 256x64), and four in two rows of two (128x128). Install the one-panel build, then pick **Three wide**, **Four wide** or **Two rows of two** on the device page.

| You have | Pick | What the board shows |
| --- | --- | --- |
| Three 64x64 panels side by side | Three wide | The wide board spread across 192 pixels |
| Four 64x64 panels side by side | Four wide | The wide board spread across 256 pixels |
| Four 64x64 panels in two rows of two | Two rows of two | The 128x64 board on top, the time, the date and a bigger ticker underneath |

With three or four panels:

- **Wi-Fi setup is through the hotspot only.** Bluetooth setup, which the Game Day app uses, stays off to leave the panels the memory they need. Scan the setup screen's QR code with your phone's camera to join the panel's hotspot. See [Set up your panel](/get-started/setup/).
- **Size the power supply for all the panels together.** Four panels at full brightness draw about four times what one does.
:::

## Chain two 64x64 panels

The firmware drives two panels as one chain. Wire the board to the input of the first panel, then run a ribbon cable from that panel's output to the input of the second. Set them side by side in one row, 128 pixels wide and 64 tall.

:::tip
If the two halves of the board show up in the wrong order, swap the two panels.
:::

:::note[Coming in the next release]
**Three or four in a row** chain the same way: the board to the first panel's input, then each panel's output to the next panel's input.

**Two rows of two** is one chain of four. Seen from the back, where the cables are: the panel with the board on it is top left, the second panel is top right, the third is under the second (bottom right) and the fourth is under the first (bottom left). Most builds turn the bottom row upside down, folded under the top row, so every cable stays short. Under **How the rows are mounted** on the device page, pick:

- **Bottom row upside down** for the folded build. A new panel starts with this one.
- **All upright** if every panel stands the right way up, with a longer cable from the second panel back to the third.
- **Top row upside down** if the top row is the one you turned over.

If part of the board shows upside down, the choice doesn't match your wiring:

| You picked | What you see | Pick instead |
| --- | --- | --- |
| **Bottom row upside down** | The bottom half upside down | **All upright** |
| **Bottom row upside down** | Both halves upside down | **Top row upside down** |
| **All upright** | The bottom half upside down | **Bottom row upside down** |
| **All upright** | The top half upside down | **Top row upside down** |
| **Top row upside down** | Both halves upside down | **Bottom row upside down** |
| **Top row upside down** | The top half upside down | **All upright** |
:::

## Change the layout

In the Setup card on the device page, click the picture that matches your panels. The panel saves the choice and restarts with the new layout. Nothing needs reflashing.

**Factory reset** returns the layout to the build's default. The one-panel build starts as one 64x64 panel. A board installed with the 128x64 choice switches to the one-panel build at its first update, so after a factory reset on that board, pick the 128x64 layout again.

## Power

Power the panels from a 5 V supply rated for them. The panel's listing gives the current it draws at full brightness; size the supply for the panel maker's rating, and add up both panels if you chain two.

:::note[Coming in the next release]
With three or four panels, add up all of them. A supply that is fine for one or two can sag with four at full brightness, and the panel then holds its brightness down as described below.
:::

A new install starts at about half brightness (128 of 255). Change it with the Brightness setting on the device page.

If the panel restarts twice in a row because its power dipped, as on a phone charger or a supply too weak for it, it holds its brightness at no more than about a quarter of full so it stays on. A small plug pulses in its top right corner, and every two minutes the bottom line says **Check power supply**, until you fix the supply and press **Clear** on the device page. A panel that runs hot dims itself a step at a time and says **Too warm, dimming** until it has cooled and is back at your brightness. See [Heat and power protection](/using/brightness-and-clock/#heat-and-power-protection).

## Ribbon cables and wiring checks

If part of the panel is dark or shows the wrong colors, reseat the ribbon cable at both ends first. R1, G1 and B1 carry the top half of the panel and R2, G2 and B2 the bottom half, so a problem in one half points at those three wires. If the whole panel flickers or the colors are off, try 5 V level shifters (see [Boards](/diy/boards/)).

To check every pixel, run the test screen described in [What's different](/diy/whats-different/#no-factory-check). For other symptoms, see [Troubleshooting](/help/troubleshooting/).
