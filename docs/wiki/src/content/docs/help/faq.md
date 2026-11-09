---
title: "FAQ"
description: "Common questions about Game Day Scoreboard: the offseason, using it outside, Home Assistant, how often it checks scores, running costs, favorites, the remote, updates and where to get support."
sidebar:
  order: 4
---

## My team's season is over. What does the panel show?

A clock in your team's colors. Under the time is your team's next game once it's on the schedule, like **Next: at PHI Mar 26 7:05 PM**, or the date until then. Nothing is wrong with the panel, and it picks the next season up by itself.

Outside football, the clock comes up once the next game is more than 10 days away. In football it comes up only when ESPN lists no next game, so a bye week never shows it.

To keep the board busy, switch to **Favorite teams** with teams from leagues that are in season, or to **Live games** for whatever is on right now. See [Modes](/using/modes/).

## Does it work during the offseason?

Yes. Between seasons it shows the clock above, with your team's next game when ESPN has one. You can also show the clock page any time: set **Showing** in the Panel card on the device page. See [Brightness and the clock](/using/brightness-and-clock/).

## Can I use it outside?

The panel is made for indoor use. Keep it, its controller and its power supply out of rain and damp. On a covered patio, put it somewhere out of the wind. It's light, and a gust can knock it over.

## I don't have an iPhone. Can I still set it up?

Yes. Any phone's camera does it, and you don't need an app. While the panel shows **SETUP**, point the camera at the QR code and tap to join the panel's own hotspot, then pick your Wi-Fi network on the setup page that opens, enter the password and save. If no page opens, open `192.168.4.1` in a browser. You can also join the hotspot from any phone or laptop by the name the panel shows, like `Game Day 2f6a70`.

Once the panel is on your Wi-Fi, its QR code opens the device page, where you pick your team and change every setting. You can also open the panel's address in a browser. See [Set up your panel](/get-started/setup/).

## Does it need Home Assistant?

No. The panel runs on its own and gets its scores from ESPN by itself. If you use Home Assistant, it finds the panel on your network and you can automate the team, mode, power, brightness and more. See [Home Assistant](/using/home-assistant/).

## How often does it check scores?

| When | How often |
| --- | --- |
| During a game | Every 10 seconds, about as often as ESPN updates |
| In the hour before the start | Every minute |
| Further from the start | Every 15 minutes |
| After the final | Every minute for half an hour, ten minutes for baseball, then it looks up the next game |

In Live games with nothing on, the panel waits for the next start time instead of checking every few minutes. When a read fails, it waits longer before the next try.

A golf tour or a racing series has its own schedule, like every minute during a round of golf and every 30 seconds during a race. See [Golf](/using/golf/#how-often-it-updates) and [Racing](/using/racing/#how-often-it-updates).

## What does it cost to run?

Nothing beyond the panel and its electricity. There's no subscription and no account. The scores come from ESPN's public feed, and the panel reads them itself over your Wi-Fi.

If your internet plan has a data cap, **ESPN data** in the Device card on the device page shows how much the panel has downloaded since it started.

:::note[Coming in the next release]
Minor league teams' scores come from MLB's public stats service instead, also free. The row is called **Data used** and counts both.
:::

:::note[Coming in the next release]
AHL and ECHL scores come from HockeyTech, the stats service the two leagues use for their own websites, also free. **Data used** counts them too.
:::

## Can I follow more than one team?

Yes. **Favorite teams** holds up to 16 teams from any league, in priority order. Turn on **Only today's games** to cycle through only the favorites playing today. When two favorites play at once, the panel sticks with the higher one, or alternates between them if you choose **Alternate**. See [Favorite teams](/using/favorites/).

A golf tour or a racing series can't be a favorite yet. Follow it in **My team**. See [Golf](/using/golf/) and [Racing](/using/racing/).

## Can I use a remote?

Yes, a WiZ WizMote, the small battery remote sold for WiZ smart bulbs. It talks to the panel directly, with no hub. To pair it, open the Remote card on the device page or in the app, turn on **Discovery**, then press any button on the remote.

- **ON** and **OFF** turn the panel on and off.
- The brightness buttons make it brighter or dimmer, and the night button dims it right down.
- Buttons **1** to **4** jump to your first four favorite teams.

See [Remote](/using/remote/).

## Will it update itself?

It checks for a new release every six hours, and the device page shows an **Install** button when one is out. If you turn on **Install updates automatically** in the Device card, the panel installs a new release on its own between 3 and 5 AM, never while a game it follows is live. That switch is off unless you turn it on.

Your team and settings stay through an update. See [Updates](/using/updates/).

## Where do I get support?

Support is on GitHub: [open an issue](https://github.com/ApolloAutomation/gameday-scoreboard-main/issues). See [Get support](/help/support/) for what to include.
