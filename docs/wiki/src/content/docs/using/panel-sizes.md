---
title: "Panel sizes and layouts"
description: "The panel arrangements the firmware drives, from one 64x64 panel to four, what each layout shows, the second area, splashes, and how to change the layout on the device page."
sidebar:
  order: 11
---

One firmware drives six arrangements of HUB75 LED panels. The panel size is a setting on the device page, not a different install.

| Arrangement | Layout | Pick on the device page |
| --- | --- | --- |
| One 128x64 panel | Wide | **One 128x64 panel** |
| Two 64x64 panels side by side | Wide | **One 128x64 panel** (the picture says "or two 64x64 side by side") |
| One 64x64 panel | Single | **One 64x64 panel** |
| Three 64x64 panels side by side | Wide, 192x64 | **Three wide** |
| Four 64x64 panels side by side | Wide, 256x64 | **Four wide** |
| Four 64x64 panels in two rows of two | Two rows, 128x128 | **Two rows of two** |

The Game Day Scoreboard ships with one 128x64 panel, and its firmware starts on the wide layout. Built your own? See [Panels](/diy/panels/).

With three or four panels, Bluetooth setup is off, so the panel sets up Wi-Fi through its hotspot only: scan the QR code on its setup screen with your phone's camera. Power them from a supply rated for all of them. See [Panels and power](/diy/panels/).

## What each layout shows

### Wide: 128x64

The logos sit at the far edges with the abbreviations and big scores between them. Under the scores is the game clock, and under that the situation row: down and distance in football, red in the red zone; the count, a diamond of the runners and the outs in baseball; the latest goal in soccer; the playoff series in basketball. The records sit under the logos, with football's timeout pips or soccer's red card marks under them. The ticker runs along the bottom.

A ranked college team's rank goes in front of its record under the logo, like "#5 9-1", when the two fit, and keeps clear of the game clock between them. A pro team can show its seed or place there instead of its record. See [Ranks and standings](/using/ticker/#ranks-and-standings).

On the start-up screen, the QR code sits on the left half with the words beside it. It joins the panel's hotspot during setup, once the hotspot is up, and opens the device page once the panel is on Wi-Fi. Without a QR code up, the words use the whole width.

### Three or four wide: 192x64 and 256x64

The wide layout spreads across the whole width: the logos at the far ends, the abbreviations and big scores between them, the game clock and the situation row in the middle, and the ticker along the whole bottom. A golf or racing leaderboard sits in the middle. The game clock is far from the records under the logos, so a rank or a standing there always has its 7 characters. A splash has the team's logo at the left end, its words in the middle with a touchdown's or a win's fireworks behind them, and the points at the right end. On the start-up screen, the words take everything to the right of the QR code. With a [second area](#the-second-area) picked, the board takes the left two panels, seen from the front, and is laid out as on one 128x64 panel.

### Two rows of two: 128x128

The top half is the 128x64 board, the same as on one 128x64 panel, its ticker along its bottom with your [favorite players](/using/favorite-players/)' stat lines and your bets in it as on any board. The bottom half is the [second area](#the-second-area), which starts on **Clock and date**: the time and the date, and under them its own ticker in bigger letters with your next games. The heat and power line, **Too warm, dimming** or **Check power supply**, goes along the bottom edge. Under the off-season clock or the idle display's clock, which already show the time, the bottom half leaves the time out: it shows its ticker, with the date above it when the clock has your next game under the time instead of the date. The board's splashes, a favorite player's celebration and a [big play in another game](/using/modes/#big-plays-in-other-games) cover all four panels, with the logo, the words and the points down the middle and the fireworks behind them. The start-up and setup screens sit in the middle of the four panels.

### Single: 64x64

The two logos sit side by side at the top, with the abbreviations and scores stacked under them. During play the game clock takes the abbreviations' place, and the gold possession line in football goes under the score. Baseball's diamond sits between the two scores with the outs as three dots under it.

A single panel has no room for the records, the timeout pips, the red card marks or the situation row, so down and distance, the count and the soccer goals are in the ticker only, when their switches are on. See [The ticker](/using/ticker/).

Before and after the game, a ranked college team's rank goes in front of its abbreviation, like "#5 OSU", when the two fit in the 6 characters there. See [Ranks and standings](/using/ticker/#ranks-and-standings).

The off-season clock shows the date under the time, and the next game goes in the ticker. On the start-up screen, the QR code and the words take turns.

## The second area

Two rows of two, and three or four panels in a row, can show something else beside the board, with a ticker of its own, so the two never show the same line.

| Arrangement | The second area | Starts on |
| --- | --- | --- |
| Two rows of two | The bottom half, 128x64 | **Clock and date** |
| Four wide | The right two panels seen from the front, 128x64 | **Nothing: the board fills the row** |
| Three wide | The right panel seen from the front, 64x64, laid out like one 64x64 panel | **Nothing: the board fills the row** |

One or two panels have no second area, and the device page doesn't offer one. With any choice but **Nothing**, the board takes the left two panels and is laid out as on one 128x64 panel. Pick the choice under **Shows** in the Quick settings card on the device page; it takes effect at once, with no restart. See [The device page](/using/device-page/#settings).

| Choice | What it shows |
| --- | --- |
| **Clock and date** | The time and the date, and in its ticker your next games, leaving out the game on the board. While the board shows its own clock, the second area shows the date and its ticker, never a second clock. |
| **Today's scores** | Recent final scores and the games still to come today for your team's and your favorites' leagues, or the leagues you picked for Live games, as the idle display's **Scores** shows them. When the board shows the scores too, the two run half a loop apart. |
| **A second team's game** | Another team, picked from the same chooser as a favorite: its next game with the start time in its ticker, its live game with its own celebrations in the second area only (the other side's too with **Opponent scores too** on), and its final for the usual wait, then its next game. A Premier League or MLS club's [cup games](/using/soccer/#cup-games) count too, as on the board. It can't be your own team, a golf tour or a racing series, and it doesn't need to be a favorite. When its game is the one on the board, the second area shows the clock and says so. Until you pick a team, the clock, with a line asking for one. |
| **Rotating live games** | The other live games in the leagues you follow, each for about 8 seconds, with the other games' scores in its ticker. Your team's game on the board is never one of them. With **Switch to big plays in other games** on, a big play in one of them shows in the second area for the **Stay on a big play** time, with its celebration there, then the rotation goes on; the board never leaves its own game for one while this choice is on. With nothing else live it shows today's scores, or the clock when there are none. |

- **Celebrations.** A splash in the second area covers that area only, as a flash of the team's color with its logo, its points and its words, and never with fireworks; the board's own splashes cover the board, and on two rows of two all four panels. Another big play doesn't move the second area while its splash is still up.
- **The board keeps its own.** What the second area shows never changes what the board shows, its ticker or its celebrations. The panel reads what the second area needs between the board's own reads, and only when it fits before the board's next one, so a live game on the board is never read late for it.
- **Data.** **Clock and date** reads nothing more. **Today's scores** reads each league's games about every half hour while games are on, and at the next start time otherwise; while the board's game is live, a college league's whole day, which is large, waits until it ends. **A second team's game** reads its game every 10 seconds while it's on, or every 15 while the board's game is live too, about 2 to 7 MB an hour, and every minute or every 15 minutes before it starts. **Rotating live games** reads the other games about every 45 seconds: in Live games the same reads **Switch to big plays in other games** makes, and in My team and Favorite teams about 0.7 to 1.6 MB an hour for each game, up to eight.
- **Night hours and Off or dim.** In the night hours **Today's scores** and the rotation give way to the clock, while a second team's game that's on and a big play still show. The idle display decides what the board shows. **Off or dim** keeps the whole panel dark or dim, unless the second team's game is on or its final is still up, or a big play has the second area. See [Night hours](/using/brightness-and-clock/#night-hours).
- **Follow this game** holds the board on its game; the rotation keeps turning and big plays still show in the second area.
- **Bets and favorite players.** A bet on the second area's game, and your favorite players' stat lines in it, go in its ticker.
- **Minor league teams.** A [minor league baseball](/using/minor-league-baseball/), [AHL or ECHL](/using/minor-league-hockey/) team can be the second team. Its games come from MLB or HockeyTech, as on the board, and while that service fails or refuses the panel the second area waits it out on its own while the board and the rest go on. **Rotating live games** leaves minor league baseball, AHL and ECHL games out, since it checks other games on ESPN only. **Today's scores** includes their leagues when you follow them, as the idle display's **Scores** does.
- If the second area's reads keep failing, its ticker says **not updating** until one comes through. Home Assistant's Game Status and Last Play stay on the board's game.

## Splashes

A score's or a big play's splash shows the logo of the team it's about, with the points the team gained and the play's words. The logo sits on a black tile with round corners, so it stands out on a flash in the team's own color too. Overtime and basketball's final show both teams' logos, and a turnover, a red card, a lead change and the victory splash show the logo with no points. A golf or racing winner's splash has no logo and looks as before.

| Arrangement | Where the parts go |
| --- | --- |
| One 64x64 panel | The logo at the top left with the points beside it, or in the middle for a play with no points, over the words in two lines. A long favorite player's name with a two-word play takes three lines in smaller letters. There's room for the points' number only, like **+2** for a 2-run home run. Two logos sit side by side. |
| 128x64 | The logo and the points side by side in the middle, the points in large digits, over the words. |
| Three or four wide | The logo at the left end, the words in the middle with the fireworks behind them, and the points or the other team's logo at the right end, the points' number only for runs, like **+2**. |
| Two rows of two | All four panels: the logo at the top, the words across the middle and the points in large digits under them. |
| The second area | As on one 64x64 panel (three wide) or 128x64 (four wide, or the bottom half of two rows of two), over that area only, as a flash of the team's color. |

The logo is the one the panel already has for the board. If it hasn't downloaded yet, the splash shows the team's abbreviation on a badge in its colors instead, as the demo's made-up teams have, and the logo takes the badge's place if it comes in while the splash is still up. The splash never waits for it. An [AHL or ECHL](/using/minor-league-hockey/) team that shows its badge on the board, for want of a small logo, keeps it on its splash too.

## Changing the layout

1. Open the device page and find the **Setup** card. It starts folded: tap **Setup** to open it.
2. Under **Matrix**, after **Time** and **Startup**, click the picture that matches your panel.
3. The page asks before it restarts the panel. Once you agree, it says **Restarting as** and the size. The panel restarts with the new layout a second later, and the page reconnects when it's back.

For **Two rows of two**, a **How the rows are mounted** choice appears under the pictures once the panel is back: **Bottom row upside down** (folded under, where a new panel starts), **All upright** or **Top row upside down**. Changing it restarts the panel too. See [Panels and power](/diy/panels/#chain-the-panels) for the wiring.

Your team and settings stay as they were. A [factory reset](/using/buttons-and-resets/) puts the layout back to the one the firmware starts on: 128x64 for the Game Day Scoreboard and the two-panel DIY build, 64x64 for the one-panel DIY build.

## Wrong size?

If the layout doesn't match the panel, the picture looks wrong:

- **The picture shows twice, side by side** (two QR codes during setup, for example): the panel is set to one 64x64 panel but is a 128x64 display. Pick **One 128x64 panel**.
- **Half the board is blank, or only part of it shows**: the panel is set to 128x64 but is a single 64x64 panel. Pick **One 64x64 panel**.
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

If the bottom half's left and right sides are swapped, or each bottom panel shows its own part upside down, the bottom row is wired the other way round and no choice fixes it. See [Panels and power](/diy/panels/#chain-the-panels) for where each panel goes.

More fixes are in [Troubleshooting](/help/troubleshooting/).
