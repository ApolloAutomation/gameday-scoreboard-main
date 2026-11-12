---
title: "Show: My team, Favorite teams, Live games"
description: "The three things the panel can follow, how it picks a game, when it moves on, and what it shows when nothing is on."
sidebar:
  order: 3
---

The three mode buttons on the device page, **My team**, **Live games** and **Favorite teams**, choose what the panel follows. The Game Day app has the same choice. The panel changes over right away and keeps the choice through restarts.

## My team

The panel follows one team: its next game until it starts, the game while it's on, and the final after.

- **Before the game** the board shows both logos and the matchup, and the ticker gives the game's day and start time in the panel's timezone, plus the line, over/under, TV channel and venue when ESPN has them. Each sport has its own details; see [Football](/using/football/), [Baseball](/using/baseball/), [Basketball](/using/basketball/), [Hockey](/using/hockey/) and [Soccer](/using/soccer/).
- **During the game** the panel reads ESPN every 10 seconds, about as often as ESPN updates.
- **After the final** the score stays up for half an hour, or ten minutes for baseball, since game two of a doubleheader can start soon after game one. Then the panel looks up the team's next game.

Long before the game starts, the panel checks it every 15 minutes, and every minute in the last hour before it starts. If a read from ESPN fails, it waits longer before each new try.

A minor league team's scores come from MLB's stats service instead of ESPN, on the same schedule. If MLB fails or refuses the panel, the panel waits longer before asking MLB again, while everything it reads from ESPN goes on as usual, and the other way around. Game 2 of a doubleheader has no start time, so on its day the panel checks it every minute. See [Baseball: minor leagues](/using/minor-league-baseball/).

An AHL or ECHL team's scores come from HockeyTech instead of ESPN, on the same schedule, and HockeyTech is kept apart the same way: if it fails or refuses the panel, only the AHL and the ECHL wait. See [Hockey: AHL and ECHL](/using/minor-league-hockey/).

A golf tour or a racing series is followed like a team, with its tournament or race weekend in place of a game, on its own update schedule. See [Golf](/using/golf/) and [Racing](/using/racing/).

Your team is always on the left of the board.

### When your team has no game coming

Between seasons the board shows a clock in your team's colors instead of an empty scoreboard. Under the time is the next game once it's on the schedule, like "Next: at PHI Mar 26 7:05 PM", or the date until then. On a single 64x64 panel the next game goes in the ticker, since there is no room for it under the time.

Football teams get the clock only when ESPN lists no game at all, so a bye week never shows it. In the other leagues the clock comes up once the next game is more than 10 days away. A golf tour's clock is green and comes up once its next event is more than 10 days away, and a racing series' clock is red and waits until the next race is more than 45 days away. See [Golf](/using/golf/) and [Racing](/using/racing/).

### A panel with no team picked

A new panel has no team. Until you pick one, it shows live NFL and college football games as Live games would, and when nothing is on, the latest final scores and the games still to come (see [When nothing is live](#when-nothing-is-live)). The ticker asks you to pick one until you choose a team, a mode or a favorite, and the QR code that opens the panel's device page comes back over the board for 13 seconds every 3 minutes until you pick a team. See [Pick your team](/get-started/pick-your-team/).

## Live games

The panel follows a game in progress, picked at random from the leagues you choose, and moves to another one when that game ends or after **Switch every**: 30 seconds, or 1 to 30 minutes. When it moves on, it picks a different game from the one it showed last, if another one is on. Splashes show the logo of the team that scored, since there is no "your team" here. With **Opponent scores too** off, only the team on the left gets them.

Pick **30 s** for a quick look at every game that's on. A game's turn starts once the panel has the game's first score, not when it picks the game, so even a 30 second turn shows the whole game, logos and all. When another game's big play takes the board for a while, the time away doesn't count: the board comes back to the game for what was left of its turn instead of moving on at once (see [Big plays in other games](#big-plays-in-other-games)). Each switch reads the leagues' games again, so 30 seconds uses about twice the data of 1 minute. An AHL or ECHL game's score can be up to a minute old for its whole turn, since HockeyTech's server hands out the same reading for up to a minute (see [Hockey: AHL and ECHL](/using/minor-league-hockey/)). On two rows of two the ticker's bigger letters take longer to go by, so in 30 seconds a long ticker, with your favorite players' stat lines and bets in it, may not get all the way through before the next game; a faster **Ticker speed** shows more of it. It's the same setting as **Alternate** in Favorite teams.

Turn on **Favorite teams only** in the Celebrations card and only games with your team or one of your favorites get big-play splashes, like touchdowns, goals and lead changes. The victory splash still shows for every game. See [Celebrations](/using/device-page/#celebrations).

With the [second area](/using/panel-sizes/#the-second-area) showing **A second team's game**, Live games picks another game than that team's when one is on, since that team has its own area.

### Follow this game

To stay with one game, turn on **Follow this game** in the Quick settings card under the board on the device page. The panel then keeps that game up past **Switch every** and doesn't look for another game. When the game ends, the panel goes back to switching games by itself, the same as without the switch, so the final shows for about a minute. Turn the switch off to go back to switching sooner.

If the panel can't get the game from ESPN for half an hour, or ESPN still shows it in progress 8 hours after it started (or after you turned the switch on, if that was later), the panel stops following it and goes back to switching games. Changing the mode, your team, your favorites or the leagues stops following too. So does a restart, since the panel doesn't save it. The switch shows only while Live games has a game it picked on the board that hasn't ended, not while it shows your team's card or recent finals with nothing live. My team follows one team already, so it has no such switch.

While the panel follows a game, it doesn't switch to big plays in other games and doesn't check those games, so it uses no data on them, except for the second area's **Rotating live games**, which keeps checking and shows big plays there. If the board is on another game's big play when you turn the switch on, the panel goes straight back to the game it was showing and follows that one. See [Big plays in other games](#big-plays-in-other-games).

### Choosing the leagues

The league buttons under the mode buttons pick the leagues. Until you pick any, Live games follows the leagues of your team and your favorites, and the page says so. Tap a league to choose by hand; **Back to automatic** goes back to following your team and favorites. At least one league stays on.

Live games doesn't follow golf or racing. There are no Golf, Formula 1 or NASCAR league buttons, and a golf tour or a racing series as your team doesn't add golf or racing to the automatic leagues.

A **MiLB** button follows minor league games at all four levels, Triple-A to Single-A, together; you can't pick one level. A minor league team as your team or a favorite adds MiLB to the automatic leagues. If MLB's stats service fails or refuses the panel, the other leagues are still checked and MiLB waits its turn.

**AHL** and **ECHL** buttons follow those leagues' games, and an AHL or ECHL team as your team or a favorite adds its league to the automatic leagues. If HockeyTech fails or refuses the panel, the other leagues are still checked and the AHL and the ECHL wait their turn. Their finals join the rotation below.

A change takes effect right away. When you change the leagues, or change your favorites so that the automatic leagues change, the panel looks for games in the new leagues at once, without waiting for its next check, instead of leaving the old leagues' scores up.

### When nothing is live

What the panel shows depends on whether you have a team:

- **With a team picked**, it shows your team's card, the same as My team, with a line like "No live NFL games, showing DAL" at the front of the ticker. It keeps looking, and switches to a live game when one starts. When your team is a golf tour, it shows that tour's tournament, and when it's a racing series, that series' race weekend, with a line like "No live NFL games, showing F1". See [Golf](/using/golf/) and [Racing](/using/racing/).
- **With no team picked**, it rotates through the games still to come today under **Later today**, each with both logos, the start time and the TV channel, then the latest final scores for every league it follows, under headers like "Yesterday's NHL scores". When no game is left today, a "Next" card names the next matchup and its time, like "Next: BOS at NYR Thu 6:00 PM". A game drops off the Later today list once it starts.

The panel waits until the next game starts before it checks again, instead of checking every few minutes, and never waits more than 3 hours. When there has been no game in the last week and none is coming, the board shows a gray clock and the ticker says "No live games right now", or names the league when Live games follows just one, like "No live NHL games right now".

## Favorite teams

The panel cycles through up to 16 favorite teams, and a favorite's game takes the panel shortly before it starts. See [Favorite teams](/using/favorites/) for how the playlist works and its settings.

**Follow this game**, in the Quick settings card under the board, keeps the favorite on the board there, with no playlist and no other favorite's game taking over, until its final has been up for **After the final**. See [Favorite teams](/using/favorites/#when-a-game-is-on).

Favorite teams with no favorites set works like My team, and the ticker asks you to add teams.

## When nothing is on: the idle display

**Idle display**, in the Quick settings card on the device page, picks what the panel shows while none of the games it follows is on. It works in My team, Favorite teams and Live games, and the panel goes back to the game by itself when one you follow starts.

- **Next game**, what every panel starts with: each mode's own board, as this page describes. My team shows your team's next game, Favorite teams cycles through your favorites, and Live games shows your team's card, or recent scores with no team picked.
- **Clock**: the scoreboard's clock in your team's colors, with your team's next game under the time on a wide panel and in the ticker. In Favorite teams it's in the colors of the favorite that plays next, with that game. In Live games with no team picked the clock is gray, with the next game of the day when there is one. Between seasons it's the same clock as always (see [When your team has no game coming](#when-your-team-has-no-game-coming)).
- **Scores**: the latest final scores and the games still to come today, the same rotation Live games shows with no team picked (see [When nothing is live](#when-nothing-is-live)). It covers the leagues of your team and your favorites, or the leagues you picked by hand for Live games. In Live games with a team picked, it takes the place of your team's card. A golf tour or a racing series with no favorites has no scores to show, so it gets the clock.
- **My team today**: on a day your team plays, its game with both logos and the records, a countdown to the start like "in 2h 15m" where the game clock goes, and the start time in the ticker. A ranked college team's poll ranking shows with its record, like "#5 TEX 7-1". After the game the final stays up until midnight, after a restart too. In a minor league doubleheader, game 2's final takes game 1's place. On a day your team doesn't play, the clock. In Favorite teams it's the first favorite playing today, then the last final of the day. In Live games it's your team, or the clock with no team picked. A golf tour or a racing series gets the clock.
- **Off or dim**: the panel goes dark, or down to the **Dim to** level you set, with the clock on it. **Panel on** stays on and your Brightness setting doesn't change, so Home Assistant sees no change either.
- **Team logo**, for a party before or after the game: your team's logo, big, with the team's name in its colors and, under it, the next game, like "vs DAL" and "Sun 1:00 PM", or a countdown like "in 2h 15m" on game day. After a game it's the result, like "W 24-17", or ESPN's word for a game that didn't finish, like "Postponed", until the team's next game is known. On one panel the logo sits on top with the name and the game under it. On two panels or more, and on a 2x2, the logo is twice the size, filling the board's height, with the name and the game beside it. On four panels in a row, a second copy of the logo sits at the right end. While the logo downloads, or for a team the panel has no logo for, its abbreviation stands in. In Favorite teams it's the favorite that plays next, or the first favorite when none has a game coming. In Live games it's your team, or the clock with no team picked. A golf tour or a racing series gets the clock, and so do the night hours. The game takes the panel back when it starts, as with every choice.

A game counts as on once it starts. In Live games the game the panel moves to counts as on from the moment it picks it, so the clock or **Off or dim** doesn't come up at each switch while the panel reads that game. The panel keeps the final up for its usual time first: half an hour in My team (ten minutes for baseball), and **After the final** in Favorite teams, where a favorite's game also takes the panel **Take over** minutes before the start of the game, as always. **Follow this game** keeps its game up too. A splash on the board, about 7 seconds, finishes before the idle display takes the panel, so a win's splash shows whole, even with **Off or dim**. After a restart, a final the panel finds already over gets that wait only if the game started less than 4 hours ago, so an automatic update's restart in the night doesn't light up an **Off or dim** panel for last night's game. A golf tour or a racing series keeps its leaderboard up while a round or a session is on, and for half an hour after the final. Overnight between rounds, between a race weekend's sessions, and after that half hour, the idle display has the panel.

The idle display needs the time of day. What it shows waits for the panel's first board after a restart, or after you pick a new team or mode, but **Off or dim** and the night hours start as soon as the panel knows the time, and a restart keeps it. While the panel isn't on Wi-Fi, **Off or dim** leaves it lit. **Scores** reads today's games from ESPN about once every half hour while games you don't follow are on, and at the next start time otherwise, the same as Live games does. Minor league scores come from MLB and AHL and ECHL scores from HockeyTech, and while one of them fails or refuses the panel its scores wait for it, as your own team's game from it does, and the other leagues' scores go on. See [Night hours](/using/brightness-and-clock/#night-hours) for the overnight schedule.

A big play in another game counts as a game that's on (see [Big plays in other games](#big-plays-in-other-games)). The panel switches to it over the clock, the scores, **My team today**, **Team logo** or **Off or dim**, and lights up for it even when **Off or dim** has it dark. In the night hours it stays at the night brightness. After the stay the idle display has the panel back, and **Scores** comes back without reading today's games again. To keep the panel dark or on the clock while games you don't follow are on, turn off **Switch to big plays in other games**. While **Scores** shows, its reads and the checks of other games take turns between the panel's own reads, and neither holds the other up.

## Big plays in other games

While the panel shows its game, it can keep an eye on other live games too. When one of them has a big play, like a touchdown, a home run, a goal or a late lead change, the panel switches to that game, shows the splash, stays there for a minute, then goes back to what it was showing. It's on unless you turn off **Switch to big plays in other games** in the Celebrations card, where **Stay on a big play** sets how long it stays, 30 seconds to 5 minutes. See [Celebrations](/using/device-page/#celebrations).

Which games it watches depends on the mode:

- **Live games**: every other game on in the leagues Live games follows. With **Favorite teams only** on, only games with your team or a favorite.
- **Favorite teams**: your other favorites' games.
- **My team**: your favorites' games, if you have any.

It watches up to 8 games at once, your favorites' first, then the closest games, late ones ahead of early ones.

- Your team's game is never left for another game's big play from its start time on, unless a favorite plays in that game and **Switch from my team** is on. It starts off. If your team's game starts while the board is on another game, the board goes back to it. For a golf tour or a racing series, that's while a round or a session is on.
- If the game the board was showing ends while the board is on another game, its victory splash shows when the board comes back. A big play it had meanwhile doesn't splash late.
- Another big play while the board is on another game can move it on to that one during the first stay, so the board is back on its own game within two stays.
- The panel doesn't switch while the board's own splash is up, and a game it has just started watching never switches it for a score from before.
- A basketball final doesn't switch the board, since that game is over.
- Home Assistant's Game Status and Last Play stay on the game the mode follows.
- **Follow this game** stops all of it: no switch and no checks of other games while the panel follows a game. If the board is on another game's big play when you turn it on, the panel goes straight back to the game it was showing and follows that one.
- The idle display gives way to a big play in another game for its stay, at the night brightness in the night hours, even with **Off or dim**, then has the panel back. See [When nothing is on: the idle display](#when-nothing-is-on-the-idle-display).
- A bet that's on the other game already, because the panel showed that game before, shows in the ticker while the board is on it, and the checks of that game keep the bet up to date and give it its result at the final. Neither the switch nor a check ever puts a bet on a game by itself. See [Bets](/using/bets/).
- Golf and racing aren't watched, and neither are minor league baseball, AHL and ECHL games, whose scores come from MLB and HockeyTech instead of ESPN, or a soccer favorite's cup games, like the Champions League. Neither is a sport whose big-play switches in the Celebrations card are all off.

The panel checks each watched game about every 45 seconds, in the gaps between its own reads of the game on the board, timed by how long ESPN took the last time so the score on the board doesn't wait for it. When ESPN is slow, a check can hold the board's next update up for a few seconds now and then. When the gaps can't take every check in a round, the ones left out go first in the next, so no watched game goes unchecked round after round. A check ESPN doesn't answer waits for the next round. One that keeps waiting out ESPN's timeout waits a few rounds more each time, up to about 5 minutes, until ESPN answers again, and the panel pauses the checks for a few minutes if ESPN asks it to slow down. That uses more data:

- In Live games it reads each league's whole day of games at once: about 250 KB each time on an NFL Sunday, up to about 20 MB an hour, and about as much on a full day of baseball. A league with nothing else on is checked again at its next start, or when a game that's on leaves the board.
- College games, which come in much larger days, and your favorites' games in My team and Favorite teams are read one at a time, 10 to 20 KB each, about 1 to 1.5 MB an hour per game. The panel looks for a favorite's game at its start time, and every few hours otherwise.
- In Live games, a college game that starts after the panel last looked for live games is watched from its next look, after **Switch every** or when the game on the board ends.

With the [second area](/using/panel-sizes/#the-second-area) on **Rotating live games**, a big play in another game shows there instead: the second area switches to that game for the stay, with the splash in the second area only, and the board never leaves its own game. **Switch to big plays in other games** then decides only whether the second area switches; the other games are checked either way, to rotate them, in My team and Favorite teams every live game in your team's and favorites' leagues, up to eight. **Follow this game** holds the board only: the rotation keeps checking and turning, and big plays still show in the second area. With any other choice, the second area's team's game is left out of the games watched.

A check every 45 seconds sees less than the board's every 10, so a big play in another game is told mostly by the score. The sport pages say what that means for each: [football](/using/football/), [baseball](/using/baseball/), [basketball](/using/basketball/), [hockey](/using/hockey/) and [soccer](/using/soccer/).

## Remote buttons and modes

Buttons 1 to 4 on a WizMote remote pick your first four favorites. In Favorite teams, a button jumps to that favorite. In My team or Live games, a button switches the panel to My team following that favorite. See [WizMote remote](/using/remote/).
