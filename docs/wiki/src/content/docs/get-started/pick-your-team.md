---
title: "Pick your team"
description: "Choose the team your panel follows on the panel's device page or from Home Assistant, in any of the nine leagues."
sidebar:
  order: 3
---

Pick your team on the panel's device page. Home Assistant can change the same setting.

## From the device page

1. Open the panel's address in a browser on any phone or computer on the same Wi-Fi. It's the `gameday-xxxxxx.local` name the panel shows when it starts, with your panel's own six characters, like `gameday-2f6a70.local`. If your phone won't open a `.local` address, use the IP address the panel shows instead.
2. Tap **Change team**.
3. Pick a league tab and tap a logo, or type in **Search teams** to find a team in any league.

The page says **Now following the** and the team's name, and the panel switches. While the new team's scores load, the panel says **Looking up scores**, for up to 10 seconds, so you never see a half-empty board.

Lost the address? Hold the button on the controller for about 1.5 seconds and the panel shows it again, with the IP address, for 20 seconds.

## From Home Assistant

The **Team** and **Mode** entities change the same settings. See [Home Assistant](/using/home-assistant/).

## From the Game Day app

The Game Day app is a separate project that Apollo Automation does not maintain, and it may not find panels running this firmware. Where it does, it can pick the team too. See [The Game Day app](/get-started/the-app/).

## The leagues

| League | Teams you can pick |
| --- | --- |
| NFL | All 32 teams |
| College football (NCAAF) | Every FBS team |
| MLB | Every team |
| NBA | Every team |
| WNBA | Every team |
| Men's college basketball (NCAAM) | Every Division I team |
| NHL | Every team |
| MLS | Every team |
| Premier League (EPL) | Every team |

Soccer clubs are followed into their cup competitions too: Premier League clubs into the Champions League, Europa League and Conference League, and MLS clubs into the CONCACAF Champions Cup.

Each sport has its own board. See [Football](/using/football/), [Baseball](/using/baseball/), [Basketball](/using/basketball/), [Hockey](/using/hockey/) and [Soccer](/using/soccer/).

## The On now tab

The first tab in the team picker, **On now**, lists today's games across the leagues, straight from ESPN. They're grouped as **Live now**, **Later today**, **Final** and **Postponed or canceled**. Tap the side you want to follow and the panel switches to that team.

Your browser reads this list from ESPN, so the tab needs your phone or computer to be online, not only the panel.

## Changes take effect right away

A new team shows on the panel within a few seconds, with nothing to restart. The panel keeps your team, and every other setting, through restarts and power cuts.

## Before you pick a team

A panel nobody has picked a team for doesn't follow one. It shows live NFL and college football games, and when nothing is on, the latest final scores and when the next game starts. See [A panel with no team picked](/using/modes/#a-panel-with-no-team-picked). The app's QR code comes back over the board for 13 seconds every 3 minutes until you pick a team.

## More than one team

**My team** follows the one team you pick here. The device page has two other modes:

- **Favorite teams** cycles through up to 16 teams from any league, in your order.
- **Live games** follows a game in progress in the leagues you choose and moves on when it ends.

See [Modes](/using/modes/) and [Favorite teams](/using/favorites/).
