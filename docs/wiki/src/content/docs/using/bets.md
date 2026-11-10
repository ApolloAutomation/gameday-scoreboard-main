---
title: "Bets"
description: "Type in a bet or a player prop you've placed, and the panel shows in the ticker whether it's covering or on track during the game and the result after the final. It never connects to a sportsbook."
sidebar:
  order: 10.5
---

Keep an eye on bets you've placed. You type each one into the **Bets** card on the [device page](/using/device-page/), and during the game the panel says in the ticker whether it's covering. The panel never connects to a sportsbook and never asks for an account or a password. It only compares the bet you typed in with the score it's already showing.

## What you can enter

| Bet | Example | How the panel checks it |
| --- | --- | --- |
| **Point spread** | Bills -3.5, Bills +7, or 0 for a pick'em | The team's score plus the spread, against the other team's score. |
| **Total, over** or **Total, under** | Over 47.5 | Both teams' points added up, against the line. Pick either team in the game, so the panel knows which game it is. |
| **Moneyline** | Bills | Whether the team is ahead. A moneyline has no line to type. |

Lines are whole or half points, like 3 or 3.5, and a comma works in place of the point, like 3,5. Spreads go up to 99.5 either way, and totals up to 999.5. The panel keeps up to eight bets. Golf tours and racing series can't take a bet. Minor league teams can: they're in the chooser's **MiLB** tab, and search finds them by level too, like "double-a". So can AHL and ECHL teams, in its **AHL** and **ECHL** tabs. Their games aren't in **On now**, so pick the team from its tab or search for it.

## Adding a bet

1. Open the device page and find the **Bets** card, after the Favorites and Favorite players cards. If it's folded, as it is while it holds no bets, tap **Bets** to open it.
2. Tap **Pick a team**. The team chooser opens, and its **On now** tab lists today's games, so you can tap the side you bet on.
3. Choose the kind of bet: **Point spread**, **Total, over**, **Total, under** or **Moneyline**.
4. Type the line. For a spread, tap **−** if the team is the favorite or **+** if it's the underdog, then type the points, like 3.5. Type 0 for a pick'em. For a total, type the game's total, like 47.5.
5. Tap **Add bet**.

The **−** and **+** buttons are there because a phone's number pad often has no minus key. On a computer you can type the minus or plus with the points instead, and the matching button lights up.

If something is missing or doesn't fit, the card says so before anything is saved, like **Pick the team first.**, **Tap − if the team is the favorite or + if it's the underdog.**, **Lines go in whole or half points, like 3 or 3.5.**, **A total has no + or -. Type it like 47.5.**, **You have that bet already.** or **All 8 bets are in use. Remove one first.**

To take a bet off, tap the remove button (✕) next to it.

## Which game a bet is for

A bet is for its team's game that's on now, or its next game, so type a bet in after the team's last game has ended. The panel ties the bet to the first of the team's games it reads. If that's a later game, say Wednesday's game against your own team that's on the board, the bet moves to the team's earlier game as soon as the panel reads that one, as long as Wednesday's game hasn't started. Once its game has started, the bet stays on it, and it also stays on its game if ESPN, or MLB for a minor league game or HockeyTech for an AHL or ECHL game, moves that game to another day. A bet you type in after the team's game has ended waits for the team's next game.

If the panel never reads the game you bet on, it can't move the bet there, and the bet stays on the later game it found. The Bets card shows which game each bet is on, so remove a bet that's on the wrong game.

The panel follows bets only on games it's already reading, from ESPN or, for a minor league team, from MLB, or for an AHL or ECHL team, from HockeyTech:

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

## Player props

:::note[Coming in the next release]
A player prop is a bet on one player's numbers in a game: over or under a line on one of their stats, like Josh Allen over 250.5 passing yards, or a yes bet, like Josh Allen to score a touchdown. You type it into the same **Bets** card, and the ticker follows it like a bet. As with bets, the panel never connects to a sportsbook and never asks for an account or a password: it reads the game's box score from ESPN and compares it with the line you typed in.

**What you can enter.** Any player on a team in the leagues below, picked from the team's roster:

| Sport | Over or under | Yes |
| --- | --- | --- |
| Football (NFL and college) | Passing yards, passing touchdowns, completions, interceptions thrown, rushing yards, rushing attempts, receiving yards, receptions, rushing and receiving yards, tackles, sacks, kicking points | Anytime touchdown |
| Basketball (NBA, WNBA and men's college) | Points, rebounds, assists, threes made, steals, blocks, and points, rebounds and assists added together, or any two of them | None |
| Baseball (MLB) | Hits, runs, RBIs, hits, runs and RBIs added together, and for a pitcher strikeouts, hits allowed, earned runs allowed and outs | Home run |
| Hockey (NHL) | Points, assists, shots on goal, blocked shots, and a goalie's saves | Anytime goal |
| Soccer (MLS and the Premier League) | None | Anytime goal |

An anytime touchdown counts a rushing or receiving touchdown, or one scored on a return or by the defense. A touchdown pass is the receiver's, not the quarterback's. An anytime goal never counts an own goal or a shootout.

Some props aren't there because ESPN's box score doesn't have what they need: total bases (it lists doubles and triples only by last name, not by player), first or last scorer (it has totals, not the order they came in), and a soccer player's shots, assists or cards (ESPN keeps them in a part of the game's page the panel would have to read 250 KB or more to reach, every time).

Lines are whole or half numbers, like 6 or 24.5, from 0.5 to 999.5, with no + or -. A comma works in place of the point. A whole number can push. A yes prop has no line. The panel keeps up to eight props, besides the eight bets. Golf, racing, minor league baseball, AHL and ECHL players can't take a prop: the rosters and box scores come from ESPN, which doesn't carry them, so those tabs are left out of the team chooser for a prop.

**Adding a prop.**

1. In the **Bets** card, choose **Player prop** as the bet.
2. Tap **Pick a team** and pick the player's team. Your browser reads its roster from ESPN, and the **Player** list fills. Or tap **Favorite players** to list just your [favorite players](/using/favorite-players/), from every team at once. Tap it again to go back to a team's roster.
3. Pick the player, then the **Stat**. The stats are the ones for the player's sport.
4. For an over or under, pick **Over** or **Under** and type the line, like 250.5. A yes prop has neither.
5. Tap **Add bet**.

If something is missing or doesn't fit, the card says so before anything is saved, like **Pick the player first.**, **A prop's line has no + or -. Type it like 24.5.**, **You have that prop already.** or **All 8 props are in use. Remove one first.** To take a prop off, tap the remove button (✕) next to it.

**Which game.** A prop is for the player's team's game that's on now, or its next game, and the panel follows it only on games it's already showing, exactly as for a bet: the game on the board, and in Favorite teams your favorites' games. Its card says **Starts when the panel shows a BUF game** until then. If the player moves to another team, the prop keeps the team you picked them from.

**In the ticker.** Each prop on the game on the board goes at the end of the ticker, after the bets:

| When | What scrolls |
| --- | --- |
| Before the start | The prop itself, like **PROP ALLEN O 250.5 PASS YDS**. |
| During the game | The player's number so far and how it's going: **ALLEN O 250.5 PASS YDS 187 ON TRACK** or **OFF TRACK**, and **HIT** once an over is already past its line. A yes prop says **ALLEN ANY TD NOT YET**, then **HIT**. |
| After the final | **WON**, **LOST** or **PUSH** with the final number, like **ALLEN O 250.5 PASS YDS 262 WON**, until midnight. |

**On track** looks at the pace: the number so far, stretched over the whole game by how much of it has been played. 120 passing yards at halftime is on pace for 240, so an over 250.5 is off track and an under on track. In baseball the game's share is counted in innings and outs, and in overtime or extra innings the number so far is the whole game's. In the first few minutes the ticker shows the number alone, since a pace means little that early. A pitcher who leaves early, or a starter who sits out the fourth quarter, keeps the pace they had, so take **ON TRACK** as a guide, not a forecast.

**How it's settled.** Half a minute after ESPN calls the game final, the panel reads the box score once more and settles the prop on the final's numbers: an over wins above the line, an under below it, and a whole-number line hit exactly is a **PUSH**. A yes prop wins with at least one. If the box score never lists the player, because they didn't play, the prop is **VOID**, as most sportsbooks treat a player who doesn't play; check your sportsbook's own rule. A soccer box score doesn't say who played, so there a player without a goal loses. A game that's postponed or canceled makes it **VOID** too. A prop goes away at midnight after its final, or a day and a half after its game's start if the panel stops showing the game before the final.

**The device page.** The Bets card lists each prop with its game and the player's number so far, and the board's tags show the props on its game: green while on track, once it's hit and after a win, red while off track or after a loss, and gold for **NOT YET**, a push or a void.

**Data.** While a game with a prop is on, the panel reads that game's box score from ESPN about once a minute, and once more after the final. It stops reading each one at the end of the box score, the first part of ESPN's game page: about 25 KB for basketball, 40 to 60 KB for football and hockey, and about 190 KB for baseball, out of a page of 250 to 950 KB, plus at most about 20 KB the connection already had on its way. That's about 1.5 MB an hour for basketball, 3 MB for football and hockey and 11 MB for baseball, on top of the 3 to 7 MB an hour the board's own reads of a live game take. The board's own reads come first: a box score is read only in a gap that ends before the board's next read, so the score never waits for it. A soccer prop reads nothing more: the goals are in the game read the panel already makes. Props are saved on the panel, so they stay through a restart or an update.
:::
