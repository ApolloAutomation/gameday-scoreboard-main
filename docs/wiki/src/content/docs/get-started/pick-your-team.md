---
title: "Pick your team"
description: "Choose the team your panel follows on the panel's device page or from Home Assistant, in any of the twelve leagues, or a golf tour or a racing series."
sidebar:
  order: 3
---

Pick your team on the panel's device page. Home Assistant can change the same setting.

## From the device page

1. Open the device page on any phone or computer on the same Wi-Fi. On a phone, point the camera at the QR code the panel shows when it joins your Wi-Fi. It opens the page by IP address, so there's no `.local` name to type. Or open the panel's address in a browser: the `gameday-xxxxxx.local` name the panel shows when it starts, with your panel's own six characters, like `gameday-2f6a70.local`. If your phone won't open a `.local` address, use the IP address the panel shows instead.
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
| Minor league baseball (MiLB) | Every Triple-A, Double-A, High-A and Single-A team, 120 in all |
| NBA | Every team |
| WNBA | Every team |
| Men's college basketball (NCAAM) | Every Division I team |
| NHL | Every team |
| AHL | All 32 teams |
| ECHL | All 30 teams |
| MLS | Every team |
| Premier League (EPL) | Every team |

The **MiLB** tab lists the minor league teams under a heading for each level, and search finds a team by its level too, like "high-a". Their scores come from MLB's stats service instead of ESPN. See [Baseball: minor leagues](/using/minor-league-baseball/).

The **AHL** and **ECHL** tabs hold minor league hockey. Their scores come from HockeyTech, the stats service the two leagues use for their own websites, instead of ESPN. See [Hockey: AHL and ECHL](/using/minor-league-hockey/).

A **Golf** tab holds the **PGA Tour** and the **LPGA Tour**. Pick one to follow that tour's tournament each week, its leaderboard and its next event. See [Golf](/using/golf/).

A **Racing** tab holds **Formula 1** and the **NASCAR Cup Series**. Pick one to follow that series' race weekends: each session's order, the race's winner and the next race. See [Racing](/using/racing/).

Soccer clubs are followed into their cup competitions too: Premier League clubs into the Champions League, Europa League and Conference League, and MLS clubs into the CONCACAF Champions Cup.

Each sport has its own board. Golf's is a tournament's leaderboard, and racing's is a session's order. See [Football](/using/football/), [Baseball](/using/baseball/), [Basketball](/using/basketball/), [Hockey](/using/hockey/), [Soccer](/using/soccer/), [Golf](/using/golf/) and [Racing](/using/racing/). Minor league baseball and the AHL and ECHL get their sport's board too.

## The On now tab

The first tab in the team picker, **On now**, lists today's games across the leagues, straight from ESPN. They're grouped as **Live now**, **Later today**, **Final** and **Postponed or canceled**. Tap the side you want to follow and the panel switches to that team. Golf and racing aren't in it; pick a tour or a series from its own tab.

Your browser reads this list from ESPN, and today's minor league games from MLB's stats service, so the tab needs your phone or computer to be online, not only the panel. It doesn't list AHL or ECHL games: HockeyTech's feed answers the panel but not your browser, so pick those teams from their tabs.

## Changes take effect right away

A new team shows on the panel within a few seconds, with nothing to restart. The panel keeps your team, and every other setting, through restarts and power cuts.

## Before you pick a team

A panel nobody has picked a team for doesn't follow one. It shows live NFL and college football games, and when nothing is on, the latest final scores and when the next game starts. See [A panel with no team picked](/using/modes/#a-panel-with-no-team-picked). The QR code for the panel's device page comes back over the board for 13 seconds every 3 minutes until you pick a team. It opens the page by IP address: scan it with your phone's camera, on the same Wi-Fi, to pick your team.

## More than one team

**My team** follows the one team you pick here. The device page has two other modes:

- **Favorite teams** cycles through up to 16 teams from any league, in your order.
- **Live games** follows a game in progress in the leagues you choose and moves on when it ends.

See [Modes](/using/modes/) and [Favorite teams](/using/favorites/).

A golf tour or a racing series can be your team in **My team**, but not a favorite or a **Live games** league. See [Golf](/using/golf/) and [Racing](/using/racing/).
