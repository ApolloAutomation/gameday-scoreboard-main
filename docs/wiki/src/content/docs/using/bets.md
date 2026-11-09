---
title: "Bets"
description: "Type in a bet you've placed, and the panel shows in the ticker whether it's covering during the game and the result after the final. It never connects to a sportsbook."
sidebar:
  order: 10.5
---

:::note[Coming in the next release]
Bets are new in the next release. This page describes them ahead of it.
:::

Keep an eye on bets you've placed. You type each one into the **Bets** card on the [device page](/using/device-page/), and during the game the panel says in the ticker whether it's covering. The panel never connects to a sportsbook and never asks for an account or a password. It only compares the bet you typed in with the score it's already showing.

## What you can enter

| Bet | Example | How the panel checks it |
| --- | --- | --- |
| **Point spread** | Bills -3.5, Bills +7, or 0 for a pick'em | The team's score plus the spread, against the other team's score. |
| **Total, over** or **Total, under** | Over 47.5 | Both teams' points added up, against the line. Pick either team in the game, so the panel knows which game it is. |
| **Moneyline** | Bills | Whether the team is ahead. A moneyline has no line to type. |

Lines are whole or half points, like 3 or 3.5, and a comma works in place of the point, like 3,5. Spreads go up to 99.5 either way, and totals up to 999.5. The panel keeps up to eight bets. Golf tours and racing series can't take a bet. Minor league teams can: they're in the chooser's **MiLB** tab, and search finds them by level too, like "double-a".

## Adding a bet

1. Open the device page and tap **Bets** near the bottom of the page to open the card.
2. Tap **Pick a team**. The team chooser opens, and its **On now** tab lists today's games, so you can tap the side you bet on.
3. Choose the kind of bet: **Point spread**, **Total, over**, **Total, under** or **Moneyline**.
4. Type the line. For a spread, tap **−** if the team is the favorite or **+** if it's the underdog, then type the points, like 3.5. Type 0 for a pick'em. For a total, type the game's total, like 47.5.
5. Tap **Add bet**.

The **−** and **+** buttons are there because a phone's number pad often has no minus key. On a computer you can type the minus or plus with the points instead, and the matching button lights up.

If something is missing or doesn't fit, the card says so before anything is saved, like **Pick the team first.**, **Tap − if the team is the favorite or + if it's the underdog.**, **Lines go in whole or half points, like 3 or 3.5.**, **A total has no + or -. Type it like 47.5.**, **You have that bet already.** or **All 8 bets are in use. Remove one first.**

To take a bet off, tap the remove button (✕) next to it.

## Which game a bet is for

A bet is for its team's game that's on now, or its next game, so type a bet in after the team's last game has ended. The panel ties the bet to the first of the team's games it reads. If that's a later game, say Wednesday's game against your own team that's on the board, the bet moves to the team's earlier game as soon as the panel reads that one, as long as Wednesday's game hasn't started. Once its game has started, the bet stays on it, and it also stays on its game if ESPN, or MLB for a minor league game, moves that game to another day. A bet you type in after the team's game has ended waits for the team's next game.

If the panel never reads the game you bet on, it can't move the bet there, and the bet stays on the later game it found. The Bets card shows which game each bet is on, so remove a bet that's on the wrong game.

The panel follows bets only on games it's already reading, from ESPN or, for a minor league team, from MLB:

- the game on the board, in every mode
- in Favorite teams, each favorite's game

The panel's checks of other games for big plays, and the minute the board spends on another game's big play, only keep up a bet that's on that game already: the ticker and the Bets card show how it's doing while the board is on that game, and it gets its result at the final even when the board has moved on. They never put a bet on a game or move it to another one. See [Big plays in other games](/using/modes/#big-plays-in-other-games).

A bet on a team the panel isn't showing waits until a game of that team comes on the board, and the Bets card says **Starts when the panel shows a BUF game**. In Favorite teams the panel reads the game on the board most often. A favorite's game that isn't on the board is checked less often, so its bet can lag behind until that game takes the board.

## Where it shows

**The ticker.** Each bet on the game on the board goes at the end of that game's ticker:

| When | What scrolls |
| --- | --- |
| Before the start | The bet itself, like **BET BUF -3.5**. |
| During the game | **BUF -3.5 COVERING**, **BUF -3.5 NOT COVERING**, or **BUF -3.5 PUSH** while the score is exactly on the line. A total reads like **OVER 47.5 COVERING**, and a moneyline like **BUF ML COVERING** while the team is ahead. |
| After the final | **WON**, **LOST** or **PUSH**, like **BUF -3.5 WON**. A game that's postponed or canceled says **VOID**. |

A tie is a push for every kind of bet: a spread or total exactly on the line, or a moneyline game that ends level.

While the [idle display](/using/modes/#when-nothing-is-on-the-idle-display) shows something in place of the game, like the clock before the start or once the final has been up for its usual time, the panel's ticker has no bets. The device page's ticker, its board and the Bets card keep showing them.

**The device page's board.** Under the board, a tag for each bet on the game: gray before the start, green while it's covering or after it won, red while it isn't or after it lost, and gold for a push or a game called off.

**The Bets card.** Every bet with its game and where it stands: the kickoff before the start, the score during the game (your team's score first), and the final score with the result. **Started, not on the panel right now** means the game is on but the panel isn't showing it, so it can't say how the bet is doing.

Home Assistant's Game Status leaves bets out.

## When a bet goes away

After the final, the result stays until midnight in the panel's timezone, then the panel drops the bet. A bet whose game the panel stops showing before its final, because you changed the team or the mode, or the game was called off, goes away a day and a half after the game's start, unless the panel's checks of other games for big plays see that final and give the bet its result.

Bets are saved on the panel, so they stay through a restart or an update.
