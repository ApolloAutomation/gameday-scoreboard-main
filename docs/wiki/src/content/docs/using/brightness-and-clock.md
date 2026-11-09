---
title: "Brightness, panel on and off, and the clock"
description: "Set the panel's brightness, turn it off and on, dim it for the night, and switch between the scoreboard and the clock."
sidebar:
  order: 17
---

The Panel card on the device page holds the settings for the screen itself: **Panel on**, **Brightness**, **Ticker speed** and **Showing**. The Game Day app and Home Assistant have the same settings.

## Brightness

**Brightness** runs from 1 (dimmest) to 255 (brightest). A new panel starts at 128. The panel keeps your setting through restarts. It can run dimmer than your setting when it's too warm or its power supply is too weak; see [Heat and power protection](#heat-and-power-protection).

A WizMote remote's brightness buttons move the same setting, a tenth of full at a time, and **NIGHT** dims the panel right down to a night level, about 8% of full. See [WizMote remote](/using/remote/).

## Turning the panel off and on

**Panel on** turns the panel's lights off and on. While it's off, the panel keeps following the game, so the board is up to date the moment it comes back on.

You can also turn it off and on with:

- a quick press of the boot button on the controller
- **ON** and **OFF** on a WizMote
- **Power** in Home Assistant

A few things turn a dark panel back on by themselves, since they mean someone wants to look at it: picking a team or a mode in the app, on the device page or in Home Assistant, a numbered button or **NIGHT** on the remote, and the brighter button on the remote, which brings back the brightness the panel had.

The panel always comes back on after a restart or a power cut.

:::note[Coming in the next release]
With the **Idle display** set to **Off or dim**, the panel also goes dark, or down to its **Dim to** level, while none of the games it follows is on, and lights up again by itself when one starts. That isn't the same as **Panel on**: the switch stays on, Home Assistant still sees the panel as on, and your Brightness setting doesn't change. Pressing **ON** on the remote doesn't light a panel that's dark this way; pick another idle display, or turn **Off or dim** back off, to see it. The remote's brighter and dimmer buttons do nothing while the panel is dark or dimmed this way, so they can't change your Brightness without you seeing it; **NIGHT** still sets your Brightness to the night level. The panel stays lit while it isn't on Wi-Fi, so you can see its screens about getting connected. See [When nothing is on: the idle display](/using/modes/#when-nothing-is-on-the-idle-display).

With a [second area](/using/panel-sizes/#the-second-area), **Off or dim** darkens or dims the whole panel, and the panel lights up for the second area too while its team's game is on or its final is still up, or while a big play has it. What the board shows stays as the idle display says. Today's scores aren't read while the panel is dark or dimmed this way.
:::

## Night hours

:::note[Coming in the next release]
Turn on **Night hours** in the Panel card on the device page, and set **From** and **To** and a **Night brightness**. A panel starts with 10 PM to 7 AM and a night brightness of 20, the level the remote's **NIGHT** button uses. The hours can run past midnight. During them:

- The panel runs at the night brightness.
- It shows the idle display, with the clock in place of **Next game** and **Scores**. **Clock**, **My team today** and **Off or dim** stay as they are. See [When nothing is on: the idle display](/using/modes/#when-nothing-is-on-the-idle-display).
- A game you follow still shows when it's on, at the night brightness, and the panel goes back to the idle display after its final.
- A big play in another game still shows for its stay, at the night brightness, unless **Switch to big plays in other games** is off. See [Big plays in other games](/using/modes/#big-plays-in-other-games).
- On two rows of two, or three or four panels in a row, the [second area](/using/panel-sizes/#the-second-area) shows the clock in place of today's scores and the rotation of live games. A second team's game that's on, and a big play there, still show.

If you change the brightness during the night hours, on the device page, in Home Assistant or with the remote's brightness buttons or **NIGHT**, your change holds until the hours end, and the next night starts at the night brightness again. The night hours never change your Brightness setting itself, and **Off or dim** stays darker than the night brightness when its level is lower. The panel needs the time of day for them, so after a power cut they start once its clock is set. A restart keeps the clock, so a restart in the night hours, like an automatic update's, comes back at the night brightness, or dark with **Off or dim**. Picking a new team or mode at night doesn't end your brightness change either.
:::

## Showing: the scoreboard or the clock

**Showing** picks the page the panel shows: the **Scoreboard**, or a **Clock** page. The scoreboard is the page that follows your games; the clock page is a plain clock for when you want one. Home Assistant has the same choice as **Select Page**.

### The off-season clock

Separate from the Clock page, the scoreboard shows a clock of its own when there's no game to follow, so the panel isn't stuck on "No upcoming game" for months between seasons:

- **In My team**, between seasons, the clock is in your team's colors. Under the time is your team's next game once it's on the schedule, like "Next: at PHI Mar 26 7:05 PM", or the date until then. Football teams get it only when ESPN lists no game at all, so a bye week never shows it; other leagues get it once the next game is more than 10 days away.
- **In My team with a golf tour or a racing series**, the clock is green for golf and red for racing. Golf gets it once the tour's next event is more than 10 days away, and racing once the next race is more than 45 days away, or after the season's last race when ESPN has no next one listed. Under the time is "Next:" and the event or race once it's on the schedule, or the date until then. See [Golf](/using/golf/) and [Racing](/using/racing/).
- **In Favorite teams**, when none of your favorites has a game scheduled, the clock is in your first favorite's colors.
- **In Live games** with no team picked, when there has been no game in the last week and none is coming, the clock is gray.

On a single 64x64 panel the date goes under the time, and the next game goes in the ticker. See [Show: My team, Favorite teams, Live games](/using/modes/#when-your-team-has-no-game-coming).

:::note[Coming in the next release]
The same clock can stand in whenever nothing you follow is on, in season too: set the **Idle display** to **Clock**. See [When nothing is on: the idle display](/using/modes/#when-nothing-is-on-the-idle-display).
:::

## Heat and power protection

The panel holds its brightness down by itself when it gets too warm or its power supply can't keep up, and says so on the screen.

**Too warm.** If the controller's chip reaches 75 C, in a sunny window or a closed cabinet for example, the panel dims itself a step at a time: to 70%, then 45%, then 25% of your brightness, one step every two minutes while it stays hot. At 85 C it goes straight to 25%. A small thermometer pulses slowly in the top right corner, and every two minutes the bottom line says **Too warm, dimming** in orange for 15 seconds. Once the chip is back under 70 C, the panel steps back up every five minutes until it's at your brightness again. Keep it out of direct sun and give it some air behind it.

**Weak power supply.** If the panel restarts twice in a row because its power dipped, as on a phone charger or a supply too weak for it, it holds its brightness at no more than about a quarter of full so it stays on. It keeps that cap through restarts until you clear it. A small plug pulses slowly in the top right corner, and every two minutes the bottom line says **Check power supply** in red for 15 seconds. Use the power supply that came with the panel, not a phone charger or a USB port, then press **Clear** in the notice on the device page.

If both happen at once, the panel shows the power warning.

The device page says the same in a notice under the board, **Too warm, dimming** or **Check the power supply**. Under **Diagnostics** in the Device card it shows the chip temperature and how many times a power dip has restarted the panel. See [The device page](/using/device-page/#diagnostics).
