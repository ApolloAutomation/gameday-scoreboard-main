---
title: "Favorite players"
description: "Pick up to eight players: the ticker shows their stat line while they lead their game, and the celebration names them when they score."
sidebar:
  order: 4.5
---

:::note[Coming in the next release]
Favorite players are new in the next release. This page describes them ahead of it.
:::

Pick the players you follow, from any team in the football, baseball, basketball, hockey and soccer leagues the panel knows. While one of their games is on the board, the ticker shows their stat line whenever ESPN lists them among the game's leaders, and when they score, the celebration says their name, like **ALLEN TOUCHDOWN**. The panel keeps up to eight players. Golf tours and racing series have no players to pick.

## Adding a player

1. Open the [device page](/using/device-page/) and find the **Favorite players** card.
2. Tap **Pick a team**. The team chooser opens, the same one you pick teams with, without the **Golf** and **Racing** tabs.
3. Your browser reads that team's roster from ESPN, and a **Player** list appears. Pick the player.
4. Tap **Add player**.

The card lists your players with their team. To take one off, tap the remove button (✕) next to them.

If the roster can't be read, the card says **Couldn't read the** team's **roster from ESPN** and asks you to check the internet connection of the phone or computer you're using: the roster comes straight from ESPN to your browser, not through the panel. The card also says so when a player is a favorite already, or when all eight are in use: **All 8 players are in use. Remove one first.**

### The name on the panel

The panel shows a player's last name in capitals, like **ALLEN**. When a teammate has the same last name, the first initial comes first, like **J. HUGHES** and **L. HUGHES**. A name is at most 10 letters, one line of a single 64x64 panel's celebration, so a longer one is cut short (**VALDES-SCA**), and accents are left off.

If a player moves to another team, remove them and add them again from the new team's roster.

## The stat line

While a favorite player's game is on the board and live, the ticker adds the player's line from ESPN's game leaders, like **ALLEN 21/34, 241 YDS, 2 TD**, after the game's own parts and before your [bets](/using/bets/). The device page shows the same lines as tags under its board. Each player has a line only while ESPN lists them among the leaders, so a player who isn't leading anything has none, and a line can come and go during the game.

What ESPN lists as leaders depends on the sport:

| Sport | Leaders | Example |
| --- | --- | --- |
| Football | One passing, one rushing and one receiving leader for the whole game, from either team | **ALLEN 21/34, 241 YDS, 2 TD**, **COOK 19 CAR, 95 YDS** |
| Basketball | Each team's leader in points, rebounds and assists | **EDWARDS 19 PTS, 7 AST**, **GOBERT 9 REB** |
| Baseball | Each team's leading hitters | **JUDGE 1-3, HR, 2 RBI, R** |
| Hockey and soccer | None | No stat line |

A long line is cut after its last part that fits. There's no stat line before the start or after the final.

## The celebration

When a favorite player scores in the game on the board, the celebration says their name and the play, in place of the team's splash for that play, never as well. It gets the same colors and fireworks the team's splash would.

| Sport | What names the player |
| --- | --- |
| Football | A touchdown, field goal, extra point, 2-point conversion or safety on a play ESPN credits to the player: **ALLEN TOUCHDOWN**, **BASS FIELD GOAL**. A touchdown pass is the receiver's. |
| Baseball | A home run (**JUDGE HOME RUN**), a grand slam, or runs on the player's hit (**JUDGE 2 RBI**), and a walk-off that ends the game. |
| Soccer | A goal, or a penalty goal (**SAKA PENALTY GOAL**). Never an own goal. |
| Hockey | A goal. ESPN doesn't say which player scored, only the play's words, like "Kyle Connor Goal", so the panel looks for the player's name there, among your favorites on the team that scored. It names a player only when exactly one of them matches. The initial helps: **J. HUGHES** and **L. HUGHES** are told apart, but two favorites on one team with the same name and initial are not, and then the team's splash shows. |
| Basketball | Only a late lead change (the last 2 minutes or overtime) or a buzzer beater, on the player's basket: **EDWARDS BUZZER BEATER**. Each time the panel reads the game, about every 10 seconds, ESPN gives only the latest play, so most baskets are never seen, and an ordinary basket doesn't celebrate. |

The panel compares each read of the game with the one before, so a score shows when the play that scored is the latest one ESPN lists at that read. If two plays come between reads, like a touchdown and the extra point, only the latest counts, and when ESPN posts the score before the play, the team's splash shows.

On one panel the name and the play take two or three lines, like **ALLEN** over **TOUCHDOWN**. On a 128x64 display they share a line when they fit.

### Its switch

The **Celebrations** card on the device page has a **Favorite players** switch, **Scores**, with the switches for each sport. It starts on, on a new panel and on one that had its other switches set already.

- It works on its own: a favorite player's score is named even when the switch for that kind of play is off, and turning it off brings back the team's splash.
- **Opponent scores too** doesn't matter to it: a favorite on the other team is named too.
- **Favorite teams only** still applies in Live games: a game without your team or a favorite team on either side gets no splashes at all, a favorite player's included.

## Which games

Only the game on the board, in every mode. A favorite player whose team isn't on the board shows nothing, and in [Favorite teams](/using/favorites/) the favorites' games that aren't on the board are left out too. Follow the player's team, or make it a favorite, to see more of their games.

## Data and Home Assistant

The panel reads nothing more from ESPN for favorite players: no rosters and no extra pages. It keeps a little more of each read it already makes of a game, and only while a favorite player is in that game's league, so a panel without favorite players reads and keeps exactly what it did before. The rosters are read by your browser, once, when you pick a team in the card.

Home Assistant gets no new entities, and its **Game Status** leaves the stat lines out.

Favorite players are saved on the panel, so they stay through a restart or an update.
