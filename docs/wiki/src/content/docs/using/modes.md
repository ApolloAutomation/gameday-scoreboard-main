---
title: "Show: My team, Favorite teams, Live games"
description: "The three things the panel can follow, how it picks a game, when it moves on, and what it shows when nothing is on."
sidebar:
  order: 3
---

The three buttons under the board on the device page, **My team**, **Live games** and **Favorite teams**, choose what the panel follows. The Game Day app has the same choice. The panel changes over right away and keeps the choice through restarts.

## My team

The panel follows one team: its next game before kickoff, the game while it's on, and the final after.

- **Before the game** the board shows both logos and the matchup, and the ticker gives the kickoff day and time in the panel's timezone, plus the line, over/under, TV channel and venue when ESPN has them. Each sport has its own details; see [Football](/using/football/), [Baseball](/using/baseball/), [Basketball](/using/basketball/), [Hockey](/using/hockey/) and [Soccer](/using/soccer/).
- **During the game** the panel reads ESPN every 10 seconds, about as often as ESPN updates.
- **After the final** the score stays up for half an hour, or ten minutes for baseball, since game two of a doubleheader can start soon after game one. Then the panel looks up the team's next game.

Far from kickoff the panel checks the game every 15 minutes, and every minute in the last hour before it starts. If a read from ESPN fails, it waits longer before each new try.

A golf tour or a racing series is followed like a team, with its tournament or race weekend in place of a game, on its own update schedule. See [Golf](/using/golf/) and [Racing](/using/racing/).

Your team is always on the left of the board.

### When your team has no game coming

Between seasons the board shows a clock in your team's colors instead of an empty scoreboard. Under the time is the next game once it's on the schedule, like "Next: at PHI Mar 26 7:05 PM", or the date until then. On a single 64x64 panel the next game goes in the ticker, since there is no room for it under the time.

Football teams get the clock only when ESPN lists no game at all, so a bye week never shows it. In the other leagues the clock comes up once the next game is more than 10 days away. A golf tour's clock is green and comes up once its next event is more than 10 days away, and a racing series' clock is red and waits until the next race is more than 45 days away. See [Golf](/using/golf/) and [Racing](/using/racing/).

### A panel with no team picked

A new panel has no team. Until you pick one, it shows live NFL and college football games as Live games would, and when nothing is on, the latest final scores and the games still to come (see [When nothing is live](#when-nothing-is-live)). The ticker asks you to pick one until you choose a team, a mode or a favorite, and the QR code that opens the panel's device page comes back over the board for 13 seconds every 3 minutes until you pick a team. See [Pick your team](/get-started/pick-your-team/).

## Live games

The panel follows a game in progress, picked at random from the leagues you choose, and moves to another one when that game ends or after **Switch every** minutes (1 to 30). When it moves on, it picks a different game from the one it showed last, if another one is on. Splashes name the team that scored, like "DAL TOUCHDOWN", since there is no "your team" here. With **Opponent scores too** off, only the team on the left gets them.

:::note[Coming in the next release]
To stay with one game, turn on **Follow this game** under the board on the device page. The panel then keeps that game up past **Switch every** and doesn't look for another game. When the game ends, the panel goes back to switching games by itself, the same as without the switch, so the final shows for about a minute. Turn the switch off to go back to switching sooner.

If the panel can't get the game from ESPN for half an hour, or ESPN still shows it in progress 8 hours after it started (or after you turned the switch on, if that was later), the panel stops following it and goes back to switching games. Changing the mode, your team, your favorites or the leagues stops following too. So does a restart, since the panel doesn't save it. The switch shows only while Live games has a game it picked on the board that hasn't ended, not while it shows your team's card or recent finals with nothing live. My team follows one team already, so it has no such switch.

Turn on **Favorite teams only** in the Celebrations card and only games with your team or one of your favorites get big-play splashes, like touchdowns, goals and lead changes. The victory splash still shows for every game. See [Celebrations](/using/device-page/#celebrations).
:::

### Choosing the leagues

The league buttons under the mode buttons pick the leagues. Until you pick any, Live games follows the leagues of your team and your favorites, and the page says so. Tap a league to choose by hand; **Back to automatic** goes back to following your team and favorites. At least one league stays on.

Live games doesn't follow golf or racing. There are no Golf, Formula 1 or NASCAR league buttons, and a golf tour or a racing series as your team doesn't add golf or racing to the automatic leagues.

A change takes effect right away. When you change the leagues, or change your favorites so that the automatic leagues change, the panel looks for games in the new leagues at once, without waiting for its next check, instead of leaving the old leagues' scores up.

### When nothing is live

What the panel shows depends on whether you have a team:

- **With a team picked**, it shows your team's card, the same as My team, with a line like "No live NFL games, showing DAL" at the front of the ticker. It keeps looking, and switches to a live game when one starts. When your team is a golf tour, it shows that tour's tournament, and when it's a racing series, that series' race weekend, with a line like "No live NFL games, showing F1". See [Golf](/using/golf/) and [Racing](/using/racing/).
- **With no team picked**, it rotates through the games still to come today under **Later today**, each with both logos, the start time and the TV channel, then the latest final scores for every league it follows, under headers like "Yesterday's NHL scores". When no game is left today, a "Next" card names the next matchup and its time, like "Next: BOS at NYR Thu 6:00 PM". A game drops off the Later today list once it starts.

The panel waits for the next kickoff before it checks again, instead of checking every few minutes, and never waits more than 3 hours. When there has been no game in the last week and none is coming, the board shows a gray clock and the ticker says "No live games right now", or names the league when Live games follows just one, like "No live NHL games right now".

## Favorite teams

The panel cycles through up to 16 favorite teams, and a favorite's game takes the panel shortly before kickoff. See [Favorite teams](/using/favorites/) for how the playlist works and its settings.

:::note[Coming in the next release]
**Follow this game** under the board keeps the favorite on the board there, with no playlist and no other favorite's game taking over, until its final has been up for **After the final**. See [Favorite teams](/using/favorites/#when-a-game-is-on).
:::

Favorite teams with no favorites set works like My team, and the ticker asks you to add teams.

## When nothing is on: the idle display

:::note[Coming in the next release]
**Idle display**, in the Panel card on the device page, picks what the panel shows while none of the games it follows is on. It works in My team, Favorite teams and Live games, and the panel goes back to the game by itself when one you follow starts.

- **Next game**, what every panel starts with: each mode's own board, as this page describes. My team shows your team's next game, Favorite teams cycles through your favorites, and Live games shows your team's card, or recent scores with no team picked.
- **Clock**: the scoreboard's clock in your team's colors, with your team's next game under the time on a wide panel and in the ticker. In Favorite teams it's in the colors of the favorite that plays next, with that game. In Live games with no team picked the clock is gray, with the next game of the day when there is one. Between seasons it's the same clock as always (see [When your team has no game coming](#when-your-team-has-no-game-coming)).
- **Scores**: the latest final scores and the games still to come today, the same rotation Live games shows with no team picked (see [When nothing is live](#when-nothing-is-live)). It covers the leagues of your team and your favorites, or the leagues you picked by hand for Live games. In Live games with a team picked, it takes the place of your team's card. A golf tour or a racing series with no favorites has no scores to show, so it gets the clock.
- **My team today**: on a day your team plays, its game with both logos and the records, a countdown to the start like "in 2h 15m" where the game clock goes, and the start time in the ticker. After the game the final stays up until midnight. On a day your team doesn't play, the clock. In Favorite teams it's the first favorite playing today, then the last final of the day. In Live games it's your team, or the clock with no team picked. A golf tour or a racing series gets the clock.
- **Off or dim**: the panel goes dark, or down to the **Dim to** level you set, with the clock on it. **Panel on** stays on and your Brightness setting doesn't change, so Home Assistant sees no change either.

A game counts as on once it starts. The panel keeps the final up for its usual time first: half an hour in My team (ten minutes for baseball), and **After the final** in Favorite teams, where a favorite's game also takes the panel **Take over** minutes before kickoff, as always. **Follow this game** keeps its game up too. A golf tour or a racing series stays on its leaderboard through the tournament or race weekend and its final.

The idle display needs the time of day, so it starts once the panel has set its clock and put its first board up after a restart, or after you pick a new team or mode. **Scores** reads today's games from ESPN about once every half hour while games you don't follow are on, and at the next start time otherwise, the same as Live games does. See [Night hours](/using/brightness-and-clock/#night-hours) for the overnight schedule.
:::

## Remote buttons and modes

Buttons 1 to 4 on a WizMote remote pick your first four favorites. In Favorite teams, a button jumps to that favorite. In My team or Live games, a button switches the panel to My team following that favorite. See [WizMote remote](/using/remote/).
