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

With **Clock size** set to **Large**, the logos and the records under them sit four rows higher, and the game clock between the records is a third taller when it fits there. The records slide toward the edges when that makes room, so "9:59 2nd" or "Halftime" is bigger beside records of up to 6 characters, like "10-3-1", and "12:04 2nd" or "End of 2nd" beside shorter ones, like "15-3". A clock that would still reach the records stays at the normal size, and your team's seed or place gets shorter first, like "1st" for "1st NFCE". The same goes for every board 128 wide or wider, both halves of two rows of two included, and for a game in a 128x64 second area. With the board across three or four panels in a row, the longer clocks fit beside any record. See [Clock size and text size](/using/brightness-and-clock/#clock-size-and-text-size).

On the start-up screen, the QR code sits on the left half with the words beside it. It joins the panel's hotspot during setup, once the hotspot is up, and opens the device page once the panel is on Wi-Fi. Without a QR code up, the words use the whole width.

### Three or four wide: 192x64 and 256x64

The wide layout spreads across the whole width: the logos at the far ends, the abbreviations and big scores between them, the game clock and the situation row in the middle, and the ticker along the whole bottom. A golf or racing leaderboard sits in the middle. The game clock is far from the records under the logos, so a rank or a standing there always has its 7 characters. A splash has the team's logo at the left end, its words in the middle, and the points at the right end. A touchdown's or a win's fireworks go off above the words, across the middle of the row, or behind them when the words take three lines. On the start-up screen, the words take everything to the right of the QR code. With a [second area](#the-second-area) picked, the board takes the left two panels, seen from the front, and is laid out as on one 128x64 panel.

### Two rows of two: 128x128

The top half is the 128x64 board, the same as on one 128x64 panel, its ticker along its bottom with your [favorite players](/using/favorite-players/)' stat lines and your bets in it as on any board. The bottom half is the [second area](#the-second-area), which starts on **Clock and date**: the time and the date, and under them its own ticker in bigger letters with your next games. The heat and power line, **Too warm, dimming** or **Check power supply**, goes along the bottom edge. Under the off-season clock or the idle display's clock, which already show the time, the bottom half leaves the time out: it shows its ticker, with the date above it when the clock has your next game under the time instead of the date. The board's splashes, a favorite player's celebration and a [big play in another game](/using/modes/#big-plays-in-other-games) cover all four panels, with the logo, the words and the points down the middle. The fireworks go off around the logo, in the rows above the words, so the words and the points under them stay clear. A celebration with fireworks in the bottom half gets them there too, when the board isn't showing its own. See [Splashes](#splashes). The start-up and setup screens sit in the middle of the four panels.

### Single: 64x64

The two logos sit side by side at the top, with the abbreviations and scores stacked under them. During play the game clock takes the abbreviations' place, and the gold possession line in football goes under the score. Baseball's diamond sits between the two scores with the outs as three dots under it.

With **Clock size** set to **Large**, the game clock in the abbreviations' place is half again as tall, up to 10 characters like "End of 2nd". With **Text size** set to **Large** as well, baseball's inning stays at the normal size, since the tail of the p in "Top" would touch the scores. The right panel of three wide works the same way. See [Clock size and text size](/using/brightness-and-clock/#clock-size-and-text-size).

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
| **Rotating live games** | The other live games in the leagues you follow, each for about 8 seconds, with the other games' scores in its ticker. Your team's game on the board is never one of them. Picking it brings up a live game at once, and when a game in the rotation ends, the game that was next comes up in its place. With **Switch to big plays in other games** on, a big play in one of them shows in the second area for the **Stay on a big play** time, with its celebration there, then the rotation goes on; the board never leaves its own game for one while this choice is on. With nothing else live it shows today's scores, or the clock when there are none. |

**Shows** also lists the other displays, each laid out for the area's size with its own ticker:

- **F1**, the [F1 display](/using/f1/): the running order during a session, then the next session, the circuit, the weather there, the championships and the last race.
- **Stocks & Crypto**, see [Stocks & Crypto](/using/markets/): up to eight stocks, funds, indexes or coins with their prices and the day's change, a chart of one at a time and a ticker with them all. On a three-wide panel each row shows the price and the change in percent in turn, 4 seconds each.
- **Flight Tracker**, see [Flight Tracker](/using/flights/): the planes flying near the home you set on the device page. On the right panel of three wide, the nearest plane's card and a list of the nearest take turns, about 8 seconds each, as on one 64x64 panel; on the right two panels of four wide and the bottom half of two rows of two they take turns at 128x64. On three or four wide with **Nothing** picked, the Flight Tracker on the main board puts the nearest plane's card on the left and the list beside it.

While the main board shows one of these (**Showing** in the Panel card), **Shows** lists **Game Day** too, which puts the scoreboard the board would show in the second area: your team's game, or whatever the board would show, its celebrations included. The second area never shows the same display as the main board: when both would, it shows the clock. The panel reads a display's data between the board's own reads, as it does for the other choices.

- **Celebrations.** A splash in the second area covers that area only, with its logo, its points and its words, for about 7 seconds, as on the board. A splash that has fireworks on the board, like a touchdown, a home run, a goal, a buzzer beater or a win, gets them in the second area too, on black, whenever the board isn't showing fireworks of its own. Other splashes there are a flash of the team's color. If the board's own celebration with fireworks comes while the second area's is up, the board takes them and the second area's splash turns to a flash of the team's color for the rest of its time. The board's own splashes cover the board, and on two rows of two all four panels. Another big play doesn't move the second area while its splash is still up.
- **Splashes and turns.** **Rotating live games** waits for the second area's splash to finish before it goes on to the next game, and a big play's stay ends only once its splash is down, so the game after it still gets its whole 8 seconds. On two rows of two, the board's own splash covers the bottom half too, so the second area waits under it: the game or score it was showing stays and gets its whole turn once the board's splash is gone, and a splash of its own that came meanwhile shows its whole 7 seconds after it.
- **Logos come up with the score.** The second area keeps its card up until the next card's logos have downloaded, for up to about 3.5 seconds, the way the board does, and the next card still gets its whole turn. A logo still downloading after the wait shows the team's abbreviation on a badge in its color until the logo comes in. The panel starts downloading the next card's logos while the card before it is up, in **Today's scores**, **Rotating live games** and for the second team, and keeps enough logos that a loop of scores or live games downloads each logo once. A logo the board or the second area needs right now downloads before ones for later cards. With three or four panels in a row, turning the second area back on leaves it empty until its first card's logos are in, for up to about 3.5 seconds.
- **Teams with no logo.** A team with no logo at ESPN, like some smaller college teams, shows its abbreviation on a badge in its color, on the board and in the second area, and the panel doesn't ask ESPN for that logo again for a few hours.
- **The board keeps its own.** What the second area shows never changes what the board shows, its ticker or its celebrations. The panel reads what the second area needs between the board's own reads, and only when it fits before the board's next one, so a live game on the board is never read late for it.
- **Data.** **Clock and date** reads nothing more. **Today's scores** reads each league's games about every half hour while games are on, and at the next start time otherwise; while the board's game is live, a college league's whole day, which is large, waits until it ends. **A second team's game** reads its game every 10 seconds while it's on, or every 15 while the board's game is live too, about 2 to 7 MB an hour, and every minute or every 15 minutes before it starts. **Rotating live games** reads the other games about every 45 seconds: in Live games the same reads **Switch to big plays in other games** makes, and in My team and Favorite teams about 0.7 to 1.6 MB an hour for each game, up to eight.
- **Night hours and Off or dim.** In the night hours **Today's scores** and the rotation give way to the clock, while a second team's game that's on and a big play still show. The idle display decides what the board shows. **Off or dim** keeps the whole panel dark or dim, unless the second team's game is on or its final is still up, or a big play has the second area. See [Night hours](/using/brightness-and-clock/#night-hours).
- **Follow this game** holds the board on its game; the rotation keeps turning and big plays still show in the second area.
- **Bets and favorite players.** A bet or a player prop on the second area's game, and your favorite players' stat lines in it, go in its ticker.
- **Minor league teams.** A [minor league baseball](/using/minor-league-baseball/), [AHL or ECHL](/using/minor-league-hockey/) team can be the second team. Its games come from MLB or HockeyTech, as on the board, and while that service fails or refuses the panel the second area waits it out on its own while the board and the rest go on. **Rotating live games** leaves minor league baseball, AHL and ECHL games out, since it checks other games on ESPN only. **Today's scores** includes their leagues when you follow them, as the idle display's **Scores** does.
- If the second area's reads keep failing, its ticker says **not updating** until one comes through. Home Assistant's Game Status and Last Play stay on the board's game.

## Splashes

A score's or a big play's splash shows the logo of the team it's about, with the points the team gained and the play's words. The logo sits on a black tile with round corners, so it stands out on a flash in the team's own color too. Overtime and basketball's final show both teams' logos, and a turnover, a red card, a lead change and the victory splash show the logo with no points. A golf or racing winner's splash has no logo: the winner's name with fireworks.

| Arrangement | Where the parts go |
| --- | --- |
| One 64x64 panel | The logo at the top left with the points beside it, or in the middle for a play with no points, over the words in two lines. A long favorite player's name with a two-word play takes three lines in smaller letters. There's room for the points' number only, like **+2** for a 2-run home run. Two logos sit side by side. |
| 128x64 | The logo and the points side by side in the middle, the points in large digits, over the words. |
| Three or four wide | The logo at the left end, the words in the middle with the fireworks above them, and the points or the other team's logo at the right end, the points' number only for runs, like **+2**. |
| Two rows of two | All four panels: the logo at the top, the words across the middle and the points in large digits under them. |
| The second area | As on one 64x64 panel (three wide) or 128x64 (four wide, or the bottom half of two rows of two), over that area only, with fireworks when the board isn't using them, or as a flash of the team's color. |

The logo is the one the panel already has for the board. If it hasn't downloaded yet, the splash shows the team's abbreviation on a badge in its colors instead, as the demo's made-up teams have, and the logo takes the badge's place if it comes in while the splash is still up. The splash never waits for it. An [AHL or ECHL](/using/minor-league-hockey/) team that shows its badge on the board, for want of a small logo, keeps it on its splash too.

- **About 7 seconds.** A splash stays up for about 7 seconds, on the board and in the second area. A new one that comes while one is up takes its place and stays its own 7 seconds. The panel reads a live game about every 10 seconds, so a touchdown and its extra point a read later still show one after the other, with the board between them. A big play in another game never takes the board while the board's own splash is up, and the idle display, **Off or dim** included, waits for a splash to finish, so a win's splash shows whole.
- **Room around the words.** The words never touch the logo or the points: there are always at least 2 blank rows or columns between them, on every layout, in the second area, and for a favorite player's name with its play. On 128x64, and in a 128x64 second area, words too long for one line, like a long name with TOUCHDOWN, go on two lines in smaller letters, so they still clear the logo above them.
- **Fireworks beside the logo.** A splash with fireworks shows them around the logo and the points, above the words, so the words stay easy to read: across the whole width of a single panel or a 128x64 one, across the middle of three or four panels in a row, and on two rows of two in up to 64 rows above the words. On a single panel or a 128x64 one, and in a second area, the points sit on a patch of black, as the logo sits on its tile, so a burst never crosses them. Only when the words leave too little room above and below them, as three lines across the middle of three or four panels do, do the fireworks go behind them. In the second area they show whenever the board isn't using them; see [The second area](#the-second-area).

:::note[Coming in the next release]
A touchdown's, a field goal's, an extra point's, a two-point conversion's, a home run's or a goal's splash can start with a short pixel-art play across the whole setup: every panel of three or four in a row, and all four of two rows of two, with bigger players. A goal's long GOOOAAALLL fits the width: GOOOAL on a single panel, and in letters twice as big on two rows of two; more letters on 128x64 and on three or four panels in a row. A caption across the top says what happened and the score while it plays: about 14 letters on a single panel and on two rows of two, where its letters are twice as big, so the words are the short ones, like **30 YD TD PASS**; about 30 on 128x64, and more on three or four panels in a row, room for the scorer's name and the long words. The splash then stays its full 7 seconds. See [Play animations](/using/football/#play-animations), and the [soccer](/using/soccer/#goal-animation) and [hockey](/using/hockey/#goal-animation) goals.
:::

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
