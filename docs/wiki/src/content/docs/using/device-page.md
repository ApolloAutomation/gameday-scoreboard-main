---
title: "The device page"
description: "The web page your panel serves: a live copy of the board, the team chooser, every setting and firmware updates."
sidebar:
  order: 2
---

Every panel serves its own web page. Open it in a browser on any phone or computer on the same Wi-Fi, at the `gameday-xxxxxx.local` name the panel shows when it starts (your panel's six characters, like `gameday-2f6a70.local`), or at its IP address if your phone won't open `.local` names. The QR code the panel shows after it joins Wi-Fi opens this page at the IP address: point your phone's camera at it. Hold the button on the controller for about 1.5 seconds and the panel shows its name and IP address, without the QR code. See [Wi-Fi](/using/wifi/) for more on the address.

The page is plain HTML built into the firmware, so there is nothing to install and no account. It works with no internet connection except for two things your browser reads from ESPN itself: the team logos and the **On now** list. The Game Day app shows the same settings, so a change in one shows up in the other.

## The live board

The top of the page mirrors the panel: both logos, the score, the game clock, down and distance or the count and outs, timeout pips, the ticker line and the last play. A label above it says **UPCOMING**, **LIVE**, **FINAL** or **NO GAME**, and bowl and playoff games show their name, like "Rose Bowl", under the score. When the panel splashes a score, the page shows the same words for a few seconds.

While a golf tour is your team, the board shows its tournament's leaderboard in place of the two teams: the round, then up to 10 rows with the place, the name, the score to par (red under par, blue over) and the holes played. Before a tournament it shows the days, and after a final the tour's next event. See [Golf](/using/golf/).

While a racing series is your team, the board shows the order of the session on track or the latest one: the session, then up to 10 rows with the place, the name, the car number and the gap. Before and between sessions it shows the next session and when it starts, and after the race the next race weekend. See [Racing](/using/racing/).

:::note[Coming in the next release]
A **Follow this game** switch sits under the board while the panel shows a game Live games picked, or a favorite's game in Favorite teams, that hasn't ended. It doesn't show over your team's card or recent finals while Live games has nothing live. Turn it on to keep that game on the panel instead of moving on, and the switch reads **Following this game**. The panel goes back to switching games once the game ends, or to your favorites once its final has been up for **After the final**, or when you turn the switch off. A restart turns it off, since the panel doesn't save it. See [Live games](/using/modes/#live-games) and [Favorite teams](/using/favorites/#when-a-game-is-on).

While the panel follows a game it doesn't switch to big plays in other games, or check those games at all. When the board is on another game's big play for a moment, the switch still shows, and the line under it says so: turning it on takes the panel straight back to the game it was showing and follows that one, not the other game. If that game has ended in the meantime, the switch doesn't show, since there's nothing left to follow. See [Big plays in other games](/using/modes/#big-plays-in-other-games).
:::

:::note[Coming in the next release]
Under the board, a tag for each bet you typed into the **Bets** card that's on the game on the board: the bet before the start, then whether it's **COVERING**, **NOT COVERING** or a **PUSH**, and after the final **WON**, **LOST** or **PUSH**. See [Bets](/using/bets/).
:::

:::note[Coming in the next release]
While the game on the board is live, a tag under the board for each of your favorite players that ESPN lists among its leaders, with their stat line, like **ALLEN 21/34, 241 YDS, 2 TD**: the same line the panel's ticker shows. See [Favorite players](/using/favorite-players/).
:::

When nothing is on, the board says why. When the panel has a reason of its own, the page shows the same words the panel shows, like **Can't reach ESPN** or **Getting ready**. Otherwise it says **No upcoming game for this team**, **No live games right now** or **None of your favorites has a game coming up**, or, with nothing on the schedule, **No upcoming event for this tour** for a golf tour and **No upcoming race for this series** for a racing series. See [Panel messages](/help/panel-messages/) for what the panel's own messages mean.

Three warnings can appear on the board:

- **data stale**: the panel has missed three reads from ESPN in a row, so the score may be old.
- **Wi-Fi needs a sign-in**: the panel's Wi-Fi wants a sign-in on a web page, like many hotel and dorm networks, and the panel can't sign in. It shows in place of **data stale**. An empty board says **This Wi-Fi needs a sign-in. Use a home network or a hotspot.**
- **Panel not answering since** and a time: the page can't reach the panel. The board dims until it answers again.

When the panel holds its brightness down by itself, a notice under the board says why: **Too warm, dimming**, or **Check the power supply** with a **Clear** button to press once you've fixed the supply. See [Heat and power protection](/using/brightness-and-clock/#heat-and-power-protection).

The dot at the top right says **live** while the page is connected to the panel and **offline** when it isn't.

:::note[Coming in the next release]
A ranked college team shows its rank in front of its abbreviation on the board, like "#5 OSU", and so do the Favorite teams list and the **On now** list. A college football team's Up next list shows a ranked opponent's rank too, like "@ #17 SMU". On a phone whose screen is too narrow for the rank beside the abbreviation, the board leaves it out. See [Ranks and standings](/using/ticker/#ranks-and-standings).
:::

## Up next

In My team, a list under the board shows the next three games after the one on the board, with the opponent, the day and time (in your browser's time) and the TV channel. It appears for football and soccer teams. MLB, NBA, WNBA, men's college basketball and NHL season schedules are too large for the panel to read, so those teams have no Up next list.

In Favorite teams, the list has one row per favorite: the live score, the final, or the next kickoff and channel. The row the panel is showing now is highlighted. **No game scheduled** means ESPN lists no game for that team. **Not checked yet** means the panel hasn't read that team from ESPN yet, for example just after it starts or right after you add a favorite.

## Choosing a team

Tap **Change team** to open the team chooser. It has a search box and a **Close** button at the top, and under them a row of tabs, one per league: **NFL**, **NCAAF** (college football), **MLB**, **MLS**, **EPL** (Premier League), **NBA**, **WNBA**, **NCAAM** (men's college basketball) and **NHL**, then **Golf** (the **PGA Tour** and the **LPGA Tour**) and **Racing** (**Formula 1** and the **NASCAR Cup Series**). ESPN has no NASCAR logo, so that tile has none. On a narrow screen the tabs wrap onto more lines, so every league and the **Close** button stay on the screen, even on a small phone. Tap a logo to follow that team, tour or series. Search looks through every league at once.

The first tab, **On now**, lists today's games in every league except golf and racing, under **Live now**, **Later today**, **Final** and **Postponed or canceled**. Tap the side you want to follow. The chooser opens on **On now** when a game is live, and on your team's league otherwise.

The same chooser adds favorites: **Add a favorite** in the Favorites card opens it, and a tap adds the team to the end of your list. The **Golf** and **Racing** tabs are hidden there, since a tour or a series can't be a favorite yet. See [Favorite teams](/using/favorites/).

:::note[Coming in the next release]
**Pick a team** in the Bets card opens the chooser too, to pick the team for a new bet. The **Golf** and **Racing** tabs are hidden there as well.

**Pick a team** in the Favorite players card opens it to pick the team whose roster to choose a player from, without the **Golf** and **Racing** tabs too.
:::

## Settings

Settings are grouped in cards. In the order the page shows them:

| Setting | What it does |
| --- | --- |
| **My team**, **Live games**, **Favorite teams** | What the panel follows. See [Show: My team, Favorite teams, Live games](/using/modes/). |
| **Change team** | Opens the team chooser. Shown in My team. |
| Leagues (Live games) | The leagues Live games picks from. Until you pick, it follows the leagues of your team and favorites. **Back to automatic** goes back to that. |
| **Switch every** (Live games) | Moves to another game in progress after this many minutes, 1 to 30. A new panel starts at 1. |
| **Take over** (Favorite teams) | How many minutes before kickoff a favorite's game takes the panel, 5 to 120. Starts at 15. |
| **After the final** (Favorite teams) | How long a favorite's final stays up before the playlist moves on, 30 seconds to 60 minutes. Starts at 30 seconds. |
| **Two games at once** (Favorite teams) | **Stick with the higher team** in your list, or **Alternate** between them every set number of minutes. |
| Ticker: **Down and distance** | Football: the down, yards to go and where the ball is. |
| Ticker: **Count and at-bat** | Baseball: the count, outs, pitcher and batter. |
| Ticker: **Match stats** | Soccer: goals, cards, possession and shots. |
| Ticker: **Last play** | ESPN's latest play in every sport. In soccer, the latest goal or card. |
| Ticker: **Pre-game odds and TV** | The line, over/under and TV channel before the start, when ESPN has them. Golf: the purse before a tournament, and the TV channel during it. Racing: the TV channel. |
| **Opponent scores too** | Splashes for the other side's scores. Your team's always show. See [Celebrations](#celebrations). |
| **Panel on** | Turns the panel's lights on or off. |
| **Brightness** | Panel brightness, 1 to 255. |
| **Ticker speed** | How fast the ticker scrolls, 1 to 10. |
| **Showing** | The Scoreboard page or the Clock page. |
| **Only today's games** | The Favorite teams playlist skips favorites that aren't playing today. |
| Favorites list | Up to 16 teams in priority order. Move a team up or down, or remove it. The first four are buttons 1 to 4 on a WizMote. |
| **Discovery**, **Unpair remote** | Pairs and unpairs a WizMote remote. See [WizMote remote](/using/remote/). |
| Matrix | Click the picture that matches your panel: **One 64x64 panel**, or **One 128x64 panel** (or two 64x64 side by side). The panel restarts with the new layout. See [Panel sizes and layouts](/using/panel-sizes/). |
| Timezone | Used for kickoff times and the clock. The page sets it from your browser unless you choose one by hand. |
| **Show how to connect when the panel starts** | After it joins Wi-Fi, the panel shows a QR code that opens this page, at the panel's IP address, then the page address: for about 18 seconds on a panel with a team, and on a new panel for 45 seconds or until you pick a team. Turn it off and a panel with a team goes straight to the scoreboard. A new panel still shows the QR code, without the address. |
| **Install updates automatically** | Off unless you turn it on. Installs a new release between 3 and 5 AM, never during a game the panel follows. See [Firmware updates](/using/updates/). |
| **Follow beta** | Off unless you turn it on. Offers beta versions, test builds of the next release, instead of releases. See [Follow beta](#follow-beta). |
| Wi-Fi networks | The networks the panel remembers, in the order it tries them, with **Forget** for each. See [Wi-Fi](/using/wifi/). |

:::note[Coming in the next release]
**Matrix** also offers **Three wide**, **Four wide** and **Two rows of two**, and the page asks before it restarts the panel with a new layout. On two rows of two, **How the rows are mounted** appears under the pictures: **Bottom row upside down**, **All upright** or **Top row upside down**. Changing it restarts the panel too. With three or four panels, Bluetooth setup is off and the panel sets up through its hotspot. See [Panel sizes and layouts](/using/panel-sizes/).

**Switch every** and the time next to **Alternate** are lists: **30 s**, then every minute from 1 to 30, showing what the panel has, 30 seconds included. They're one setting, so a change in one shows in the other. In Live games 30 seconds uses about twice the data of 1 minute, since each switch reads the leagues' games again. The Game Day app, where it works with this firmware, shows 30 seconds as 1 minute. See [Live games](/using/modes/#live-games) and [Favorite teams](/using/favorites/#when-a-game-is-on).
:::

:::note[Coming in the next release]
With **Show how to connect when the panel starts** on, the panel's IP address and `.local` name also lead the ticker for 30 seconds once the QR code and the address go. If a game is live when a panel that already has a team joins Wi-Fi, the scoreboard comes up as soon as it's ready and the address is in the ticker only.
:::

Only the ticker switches for sports your panel follows are shown. Hockey, basketball, golf and racing have no game situation line, so they have no switch of their own.

:::note[Coming in the next release]
The Panel card adds **Standing next to pro teams**: **Record** (where a new panel starts), **Seed** or **Place**. It sets what your pro team shows where its record goes, under its logo on a 128x64 board, in the ticker and on this page's board, like "Seed 4" or "1st NFCE". The other team keeps its record. A minor league team always shows its record, since MLB has no seed or place for it, and so does an AHL or ECHL team, since HockeyTech has none either. It's hidden while your panel follows only college teams, minor league baseball, AHL or ECHL teams, golf or racing. See [Ranks and standings](/using/ticker/#ranks-and-standings).

The Panel card also has a **When nothing is on** part under **Showing**:

| Setting | What it does |
| --- | --- |
| **Idle display** | What the panel shows while none of the games it follows is on: **Next game** (each mode's own board, what a panel starts with), **Clock**, **Scores**, **My team today** or **Off or dim**. The line under it says what the choice does. See [When nothing is on: the idle display](/using/modes/#when-nothing-is-on-the-idle-display). |
| **Dim to** (Off or dim) | How dim the panel goes, 0 to 255. 0, what it starts at, turns the screen off; **Panel on** stays on. |
| **Night hours** | Dims the panel overnight and shows the idle display, with the clock in place of **Scores** or **Next game**. Off unless you turn it on. |
| **From**, **To** (Night hours) | When the night hours start and end, 10 PM and 7 AM to start with. They can run past midnight. |
| **Night brightness** (Night hours) | The brightness during the night hours, 1 to 255. Starts at 20. A Brightness change during the night hours lasts until they end. See [Night hours](/using/brightness-and-clock/#night-hours). |

Under them a line says what the panel is doing about it now, like **Showing the clock, in the night hours, until a game you follow starts** or **The panel is dark until a game you follow starts**. The board at the top of the page keeps showing the game the mode follows, such as your team's next game, with that game's ticker under it, while the panel shows the idle display. Each change shows on the page at once and stays there while the panel takes it.
:::

### Celebrations

The panel fills the screen with a splash when someone scores, in the scoring team's color. Your team's always show. **Opponent scores too** turns the other side's on or off in football, baseball, hockey and soccer. Basketball only celebrates the win, so the switch makes no difference there.

:::note[Coming in the next release]
The Celebrations card has a switch for each kind of splash, grouped by sport. Like the Ticker card, it shows only the sports your panel follows, and all of them before you pick anything:

| Sport | Switches |
| --- | --- |
| Football | Touchdown, Field goal, Extra point, 2-point conversion, Safety, Interception, Fumble recovery |
| Baseball | Runs, Home run, Grand slam, Walk-off |
| Hockey | Goal |
| Soccer | Goal, Penalty goal, Red card |
| Basketball | Lead change, Buzzer beater, Overtime, Final |
| Favorite players | Scores |

They all start on. The sport pages say what each one shows: [football](/using/football/), [baseball](/using/baseball/), [hockey](/using/hockey/), [soccer](/using/soccer/) and [basketball](/using/basketball/). A minor league team counts as baseball here. Its games get **Runs** and **Walk-off** only, since MLB's minor league scores don't say how a run scored. An AHL or ECHL team counts as hockey, and its goals splash under **Goal** like an NHL team's.

**Opponent scores too** still decides whether the other side's scores and big plays splash, in basketball too now. A red card, overtime and the final show either way.

**Favorite players: Scores** shows whatever sports you follow, and names a favorite player who scores in place of the team's splash for that play. It works on its own, whatever the sport's switches and **Opponent scores too** say. See [Favorite players](/using/favorite-players/#the-celebration).

**Favorite teams only** keeps the big-play splashes in Live games to games with your team or one of your favorites on either side. In My team and Favorite teams the game on the board is always one of yours, so it changes nothing there. The victory splash has no switch and still shows for every game, as before.

Three more controls under it are for big plays in other games:

| Control | What it does | Starts at |
| --- | --- | --- |
| **Switch to big plays in other games** | The panel also checks the other live games it would show, about every 45 seconds, and switches to one for its big play. In Live games that's every game in its leagues (only your team's and favorites' with **Favorite teams only** on); in My team and Favorite teams, your favorites' games. The switches above pick which big plays count, and a sport with all of them off isn't checked. A big play shows over the idle display too. Nothing is checked while you follow a game with **Follow this game**. It uses more data. | On |
| **Stay on a big play** | How long the board stays on the other game before going back: 30 seconds, 1 minute, 90 seconds, 2, 3 or 5 minutes. | 1 minute |
| **Switch from my team** | Lets a favorite's big play take the board from your team's live game. Off, your team's game is never left from its start time on, and a golf tour's or a racing series' leaderboard isn't left while a round or a session is on. | Off |

See [Big plays in other games](/using/modes/#big-plays-in-other-games) for which games are watched and how much data it takes.

A splash fits 10 letters a line on a single 64x64 panel and 16 on a 128x64 panel, so the longer ones take two or three lines, like "PHI" over "FUMBLE RECOVERY".
:::

### Timezone

The Time line in the Setup card names the panel's timezone. While it follows your browser, opening the page from a browser in another zone moves the panel to that zone. Tap **choose manually** to pick one from the list instead; the page then leaves it alone until you tap **follow this browser**. The list covers the US zones (Eastern, Central, Mountain, Arizona, Pacific, Alaska and Hawaii), Canada Atlantic and Newfoundland, Mexico City, UK and Ireland, Central Europe, Australia Eastern and UTC. A new panel starts on US Central.

## The Favorite players card

:::note[Coming in the next release]
The **Favorite players** card comes after the Favorites card. It lists your favorite players, up to eight, each with their team and a remove button (✕). To add one, tap **Pick a team**: your browser reads that team's roster from ESPN, and a **Player** list appears. Pick the player and tap **Add player**; the card says **Added** once the panel has them. The panel shows the player's last name in capitals, with the first initial when a teammate has the same last name (more of the first name when the initial is the same too), in at most 10 letters. If the roster can't be read, the card says so: check the internet connection of the phone or computer you're using. See [Favorite players](/using/favorite-players/).
:::

## The Device card

The Device card has the buttons and the details about the panel itself.

- **Refresh scores** reads your team from ESPN again right away. Use it when the board looks wrong, for example after a game moved.
- **Check for updates** asks the update server now. See [Firmware updates](/using/updates/).
- **Reboot** restarts the panel. The page says it's restarting and reconnects when the panel is back.
- **Reset Wi-Fi** and **Factory reset** restart the panel into setup, so the page can't follow it. See [The boot button and resets](/using/buttons-and-resets/).

Under them: the **Firmware** box, the **Install updates automatically** and **Follow beta** switches, the Wi-Fi networks list, and the panel's **Address** and **Name**. The **Diagnostics** section comes last.

### Diagnostics

**Diagnostics** helps you, or support, work out why a panel misbehaves.

- **Last restart**: why the panel last started and how long ago: **Power on**, **Restart** (one the panel asked for, like **Reboot** or a layout change), **Update**, **Crash**, **Froze (watchdog)**, **Power dip (brownout)** or **USB reset**. When the panel has started more than once since the power came on, the line under it counts the starts.
- **Trouble since power on**: the last crash, freeze or power dip since the panel was plugged in, and how long ago, or **None**.
- **Free memory**: how much memory the panel has left, and the largest piece of it.
- **Chip temperature**: the controller chip's temperature, and how far the panel has dimmed itself when it's too warm. At 75 C or more, the panel dims until it cools. See [Heat and power protection](/using/brightness-and-clock/#heat-and-power-protection).
- **Power dips**: how many times the panel has restarted because its power dipped, and **brightness capped** while it holds its brightness down for that.
- **Last ESPN read**: how the last read from ESPN went, **OK**, **No answer** or an HTTP error like **HTTP 503**, and how long ago.
- **Firmware**: the version, then **stable**, or **beta** while **Follow beta** is on, and when it was built.
- **Signal**: the Wi-Fi signal now, in dBm. Under it, a graph of the last day of signal, with a dashed line where it gets weak (below -75 dBm) and red ticks where the panel was off Wi-Fi. When the signal has been weak over the last half hour, the page says **Weak signal. Move the panel closer to the router, or the router closer to the panel.**
- **Wi-Fi drops**: how many times the panel lost its Wi-Fi in the last 24 hours, or since it started if that was less than a day ago. The line under it gives the cause of the last one, like **signal lost** or **dropped by the router**, how long ago it was and how soon the panel was back.
- **Free PSRAM**: how much of the panel's second, larger memory is left.
- **ESPN data**: how much the panel has downloaded from ESPN since it started, for anyone on a capped internet plan.

:::note[Coming in the next release]
Minor league baseball adds a **MiLB** tab to the team chooser, after **MLB**, with the teams under a heading for each level, and On now lists minor league games too. A minor league team has an Up next list of its next three games. Diagnostics show **Last MLB read** once the panel has read minor league scores, and the data row becomes **Data used**, counting what the panel downloads from MLB as well as ESPN. When the panel can't reach MLB for a minor league team, the board says **Can't reach MLB**. See [Baseball: minor leagues](/using/minor-league-baseball/).
:::

:::note[Coming in the next release]
The AHL and the ECHL add an **AHL** tab and an **ECHL** tab to the team chooser, after **NHL**, with HockeyTech's logos; On now doesn't list their games, since HockeyTech's feed doesn't answer your browser. An AHL or ECHL team has an Up next list of its next three games. Diagnostics show **Last HockeyTech read** once the panel has read their scores, and **Data used** counts what the panel downloads from HockeyTech too. When the panel can't reach HockeyTech for an AHL or ECHL team, the board says **Can't reach HockeyTech**. See [Hockey: AHL and ECHL](/using/minor-league-hockey/).
:::

### Follow beta

**Follow beta** is off unless you turn it on. Beta versions are test builds of the next release, for test panels, and they can have bugs. Leave it off on a panel you just want to watch games on.

With **Follow beta** on, the Firmware box and **Check for updates** show the latest beta instead of the latest release. A beta is named for a coming release plus a build number, like **v2026.10.3-beta.20**. Betas go in build order, and every beta comes before the release with its number:

- On a beta, the panel is offered each newer beta. Tap **Install**, or turn on **Install updates automatically** and it installs new betas overnight.
- On a release, the panel waits for a beta of the next release. Until one comes out, the Firmware box says **You're ahead of the latest beta**, and nothing installs. An older beta is never offered.
- Turn **Follow beta** off while on a beta and the panel stays on that beta, and the Firmware box says **You're ahead of the latest release**, until the next release comes out. Then the panel is offered that release, and installs it overnight if **Install updates automatically** is on.

See [Firmware updates](/using/updates/#beta-versions).

## The Bets card

:::note[Coming in the next release]
The **Bets** card comes last on the page and stays folded until you tap **Bets**; the number beside it says how many of the eight bets are in use. You type in bets you've placed, and the panel never connects to a sportsbook. The card lists every bet with its game and where it stands, and has a form to add one: **Pick a team**, the kind of bet (**Point spread**, **Total, over**, **Total, under** or **Moneyline**) and the line, then **Add bet**. For a spread, tap **−** if the team is the favorite or **+** if it's the underdog, then type the points, like 3.5. The remove button (✕) takes a bet off. The panel follows bets only on games it's already reading: the game on the board, and in Favorite teams your favorites' games. See [Bets](/using/bets/).
:::
