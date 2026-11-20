---
title: "What the panel is telling you"
description: "Every message and icon the Apollo Pulse panel shows when it needs you or something is wrong, what each means and what to do."
sidebar:
  order: 3
---

The panel says what's going on in words on the screen, or in the ticker along the bottom. Find the words you see below. Your panel's own six characters, like `2f6a70`, and its own IP address stand in for the examples here.

## While it starts and sets up

These show on the start-up screen, under **APOLLO PULSE** in light blue on black, with Apollo, the Apollo Automation dog, beside the words, and a green bar under the name while the panel is busy. Your phone's camera reads every QR code here, so none of them needs an app: off Wi-Fi the code joins the panel's own hotspot, and on Wi-Fi it opens the panel's device page.

On a single 64x64 panel the words and the QR code take turns, and the screens that come before any QR code take turns with Apollo, about 4 seconds each: **Starting**, **Connecting**, **Looking up scores**, **JOINING**, **SETUP / Starting / hotspot**, **Wi-Fi / lost / reconnecting** and **Can't join / retrying**. Once a QR code is part of the screen, its word turns never show Apollo: **SETUP / Scan to / join Wi-Fi**, **Join Wi-Fi / Apollo Pulse / 2f6a70**, **Unplug it to / rejoin / saved Wi-Fi**, **Wi-Fi / lost / scan to fix** and **Can't join / scan to fix**. With three lines of words or fewer, the name is in two lines above them.

On 128x64, Apollo has the left half when there's no QR code, with **APOLLO / PULSE** in two lines beside him and the words under it, as short as on a 64x64 panel: **Connecting** and the network's name, and **Looking up / scores**. When a QR code is up, it has the left half instead, with no Apollo, and the name sits beside it in two lines above the words. The button's address screen, its countdown and **Updating** keep the whole width, with no Apollo.

On three or four panels in a row, Apollo sits at the left end and the QR code beside him. On two rows of two, Apollo or the QR code sits in the middle of the top half, with the words under it.

On Wi-Fi, the screens where the panel isn't busy, like the QR code after joining, the address and **Updating**, show a thin orange line under the name instead of the green bar.

| The panel says | What it means | What to do |
| --- | --- | --- |
| **Starting** | The panel has power and is starting up. | Wait a few seconds. |
| **SETUP / Starting / hotspot** | The panel has no Wi-Fi network yet, or you reset its Wi-Fi, and its hotspot is starting. | Wait a moment. |
| **SETUP / Scan to / join Wi-Fi**, with a QR code | The panel has no Wi-Fi network yet, or you reset its Wi-Fi, and its hotspot is up. The QR code is the hotspot's Wi-Fi code. | Point your phone's camera at the QR code and tap to join, then pick your network on the setup page. If no page opens, open `192.168.4.1` in a browser. Or join the hotspot by name (the panel shows it in turn), or use the installer's Wi-Fi dialog. See [Set up your panel](/get-started/setup/). |
| **Join Wi-Fi / Apollo Pulse / 2f6a70** | The panel's own hotspot is up, named `Apollo Pulse 2f6a70`. It takes turns with the hotspot's QR code. | Scan the code, or join that network from any phone or laptop to set it up. |
| **Unplug it to / rejoin / saved Wi-Fi** | The panel is in setup after a Wi-Fi reset and still knows its saved networks. It takes turns with **SETUP**. | Set it up on a new network, or unplug it and plug it back in to go back to the networks it knows. It goes back by itself after 15 minutes, or up to 10 minutes later if you're still on its hotspot or setting it up from the Game Day app. See [Reset Wi-Fi](/using/buttons-and-resets/#reset-wi-fi). |
| **Connecting to** and a network name (**Connecting** on a 64x64 panel) | The panel is joining a network it already knows. | Wait. |
| **JOINING** and a network name | The app sent the panel a network over Bluetooth, and it's joining it. | Wait. If the name is wrong, pick again in the app. |
| A QR code, with **Scan to / pick your / team** beside it on the 128x64 panel | The panel joined your Wi-Fi. The QR code opens its device page by IP address. | Scan it with your phone's camera, on the same Wi-Fi, and pick your team. Or wait for the address. |
| **Open gameday-2f6a70.local** | The panel's device page address. | Open it in a browser on the same Wi-Fi. |
| **Looking up scores** | The panel is loading its first scoreboard, at start-up or after you change the team. It shows for up to 10 seconds. | Wait. |

A panel joining Wi-Fi for the first time, from the hotspot's setup page or the installer's Wi-Fi dialog, says **JOINING** and the network's name. If it still isn't on after about 30 seconds, it says **Can't join** and the network's name, with **scan to fix** and the hotspot's QR code taking turns with **Join Wi-Fi / Apollo Pulse / 2f6a70**. It keeps trying that network. If you mistyped the password or picked the wrong network, scan the code and give it again on the setup page. See [The panel won't join my Wi-Fi](/help/troubleshooting/#the-panel-wont-join-my-wi-fi).

## When the Wi-Fi is the problem

| The panel says | What it means | What to do |
| --- | --- | --- |
| **Wi-Fi / lost / reconnecting** | The panel has been off its Wi-Fi for more than about 10 seconds and is trying to get back on. | If the router or internet is down, wait: the panel rejoins by itself. If you changed your router or Wi-Fi password, wait a few minutes: the panel brings up its hotspot and shows a QR code to scan (next row). |
| **Wi-Fi / lost / scan to fix**, with a QR code, taking turns with **Join Wi-Fi / Apollo Pulse / 2f6a70** | The panel is still off its Wi-Fi and its hotspot is up. It keeps trying its saved networks. The QR code is the hotspot's Wi-Fi code. | If you changed the router or the password, scan the code with your phone's camera, join, and give the panel the new one on the page that opens. If the router is only down, wait. See [New router or Wi-Fi password](/get-started/setup/#new-router-or-wi-fi-password). |
| **Wi-Fi needs / sign-in / use home or / a hotspot**, with a QR code | The panel joined the network, but the network wants a sign-in on a web page, like many bar, hotel and dorm networks. The panel can't sign in. The QR code opens the panel's device page by IP address. | Set it up on a home network or a phone hotspot. From a phone on the same Wi-Fi, scan the code and tap **Reset Wi-Fi** in the Device card to put the panel back into setup. If the page won't open, use the button's [Reset Wi-Fi](/using/buttons-and-resets/#reset-wi-fi). The message goes by itself once scores come through. |

## On the scoreboard

These show on the board or in the ticker.

| The panel says | What it means | What to do |
| --- | --- | --- |
| **Pick a team at** and an IP address | Nobody has picked a team or a mode yet. | Open that address, the device page, in a browser on the same Wi-Fi and pick one. See [Pick your team](/get-started/pick-your-team/). |
| **Getting ready** | The panel is on Wi-Fi but hasn't set its clock yet. It can't read scores before it has. | Wait a few seconds. If it lasts, see [Troubleshooting](/help/troubleshooting/#the-panel-says-getting-ready-for-a-long-time). |
| **Can't reach ESPN** | The panel is on Wi-Fi but gets no answer from ESPN, where the scores come from. | Check the internet connection. The panel keeps trying. |
| **Can't reach MLB** | The same, for a minor league baseball team, whose scores come from MLB's stats service. | Check the internet connection. The panel keeps trying, and your other teams and leagues keep updating. See [Baseball: minor leagues](/using/minor-league-baseball/). |
| **Can't reach HockeyTech** | The same, for an AHL or ECHL team, whose scores come from HockeyTech, the leagues' own stats service. | Check the internet connection. The panel keeps trying, and your other teams and leagues keep updating. See [Hockey: AHL and ECHL](/using/minor-league-hockey/). |
| **no update yet** at the end of the ticker | Three reads from ESPN in a row failed, and none has worked since the panel started. | Check the internet connection. |
| **no update for 12 min** at the end of the ticker | Three reads in a row failed. The score on the board is from that many minutes ago. | Check the internet connection. The board catches up once a read works. |
| **No upcoming game** | ESPN lists no next game for your team. | Nothing. Between seasons the panel shows a clock instead, below. |
| **No upcoming event** | Your team is a golf tour, and ESPN lists no tournament for it, now or ahead. | Nothing. The panel checks again every hour. See [Golf](/using/golf/). |
| **No upcoming race** | Your team is a racing series, and ESPN lists no race for it, now or ahead. | Nothing. The panel checks again every hour. See [Racing](/using/racing/). |
| **Looking for a live game** | In Live games, the panel is checking what's on. With one league picked, it names it, like **Looking for a live NHL game**. | Wait. |
| **No live games right now** | In Live games, nothing is being played in the leagues you picked. | Nothing. The panel shows today's later games and recent final scores, and picks up the next game when it starts. |
| **No live NFL games, showing CHI** | In Live games with nothing on, the panel shows your own team until a game starts. | Nothing. |
| **Favorites: add teams at** and an IP address, or **Favorites: add teams on the device page** | The panel is in Favorite teams with no favorites. | Add favorites on the device page, at that address, or switch to My team. See [Favorite teams](/using/favorites/). |
| **Later today** | A card before the games still to come today, each with both logos, the start time and the TV channel. | Nothing. |
| **Yesterday's NHL scores**, or another day and league | A card before that day's final scores. | Nothing. |
| **Next: BOS at NYR Thu 6:00 PM** | No game is left today. This is the next one. | Nothing. |
| A clock, with **Next:** and a date, or only the date | Your team is between seasons. The clock is in your team's colors, or gray with no team picked. A golf tour's clock is green and a racing series' is red. Under it is the next game, event or race once it's on the schedule. | Nothing. See [Brightness and the clock](/using/brightness-and-clock/). |
| A clock, with **No upcoming PGA Tour event**, **No upcoming LPGA Tour event**, **No upcoming Formula 1 race** or **No upcoming NASCAR Cup Series race** in the ticker | Your team is a golf tour or a racing series, its season is over, and ESPN hasn't listed the next event or race yet. | Nothing. See [Golf](/using/golf/) and [Racing](/using/racing/). |
| **Postponed**, **Canceled** or **Suspended** | The game ended without being played out. A canceled or postponed golf tournament or race shows its name and **Canceled** or **Postponed**. | Nothing. |
| **Sat TBD** | ESPN hasn't set the game's start time yet. | Nothing. The time shows once ESPN has it. |
| **Today TBD** for a minor league team | Game 2 of a minor league doubleheader, which has no start time until it starts. | Nothing. See [Baseball: minor leagues](/using/minor-league-baseball/). |
| **IP 192.168.1.50 \| gameday-2f6a70.local** at the start of the ticker | The panel just joined Wi-Fi, and these are its IP address and `.local` name. It goes after 30 seconds. It also runs under a message that fills the board, like **Getting ready** or **Can't reach ESPN**, which otherwise has no ticker. | To open the device page, open either one in a browser on the same Wi-Fi; use the IP address if your phone won't open the `.local` one. |

With a golf tour or a racing series as your team, a tournament or a race weekend before it starts shows its name and its days, or for a race weekend the next session, in place of a matchup. See [Golf](/using/golf/) and [Racing](/using/racing/).

A panel with no team picked also brings the QR code that opens its device page back over the board for 13 seconds every 3 minutes, until you pick one.

An AHL or ECHL team with no small logo shows its abbreviation in a box in the logo's place. See [Hockey: AHL and ECHL](/using/minor-league-hockey/).

## On the other displays and the second area

These show on Weather, the Flight Tracker, Stocks & Crypto or F1, on the main board or in the second area, or in the second area's ticker.

| The panel says | What it means | What to do |
| --- | --- | --- |
| **Set your home on the device page** | Weather has no home to show the weather for. | Set a home in the Weather card or the Flight Tracker card on the device page; the two share it. See [Weather](/using/weather/). |
| **Can't reach weather data** | Weather's reads keep failing, and it has no forecast to show. | Check the internet connection. The panel keeps trying. |
| **Set your location on the device page** | The Flight Tracker has no home to look for planes around. | Set a home in the Flight Tracker card on the device page. See [Flight Tracker](/using/flights/). |
| **Can't reach flight data** | Three reads in a row failed, and the panel has no planes from the last 3 minutes. | Check the internet connection. The panel keeps trying. |
| **Can't reach Yahoo Finance**, or **Can't reach Yahoo Finance, Kraken or Nasdaq**, in the ticker | Stocks & Crypto has no prices yet, because its reads keep failing: from Yahoo Finance, or from its backups too. | Check the internet connection. The panel keeps trying. See [Stocks & Crypto](/using/markets/). |
| **Can't reach the F1 schedule** in the ticker | F1 can't read the season's schedule, so it has nothing to show yet. | Check the internet connection. The panel keeps trying. See [F1](/using/f1/). |
| **Pick the second team on the device page** in the second area | The second area is set to **A second team's game**, with no team picked. | Pick one under **Shows** in the Quick settings card. See [The second area](/using/panel-sizes/#the-second-area). |

## When you hold the button

The button on the controller shows the panel's address and resets it. Nothing resets until you let go, and the screen says what letting go will do. See [Buttons and resets](/using/buttons-and-resets/).

| The panel says | What it means |
| --- | --- |
| **On your phone, open** the address **or** the IP address **then pick your team** | You held the button for about 1.5 seconds. The address shows for 20 seconds. On a 64x64 panel it shows the address over two lines, then the IP address and **pick a team**. |
| **Keep holding / Wi-Fi reset / in 5s**, counting down, with a bar filling | You've held it for 5 seconds. Let go now and nothing resets. |
| **Release to / reset Wi-Fi / Hold 10s to / erase all**, counting down | You've held it for 10 seconds. Let go now to reset Wi-Fi. |
| **Release to / erase all / Hold 10s to / cancel**, counting down | You've held it for 20 seconds. Let go now for a factory reset, which erases everything. |
| **Release to / cancel** | You've held it for 30 seconds. Let go now and nothing resets. |
| **Wi-Fi reset / restarting** | You let go to reset Wi-Fi. The panel restarts into setup and keeps its saved networks. |
| **Erasing all / restarting** | You let go for a factory reset. The panel erases everything and restarts into setup. |

## Icons in the corner

A small icon pulsing slowly in the top right corner means the panel is holding its brightness down. Every two minutes the bottom line says why, for 15 seconds.

| You see | What it means | What to do |
| --- | --- | --- |
| An orange thermometer and **Too warm, dimming** | The panel is running hot and has dimmed itself. | Keep it out of direct sun and give it air behind it. It goes back to your brightness as it cools. |
| A red plug and **Check power supply** | The panel restarted twice in a row because its power dipped, so it keeps its brightness low. | Use the power supply that came with it, not a phone charger or a USB port. Then press **Clear** on the device page. |

More in [Troubleshooting](/help/troubleshooting/#the-panel-is-dim-or-shows-a-thermometer-or-plug-in-the-corner).

## Red, green, blue and white screens

If the whole panel lights red, green, blue and white in turn, then a white line runs down it and a screen of details ends with **Press button**, the panel is running its factory check. Press the button on the controller once. The panel says **Button OK** and goes back to normal.

## During an update

While an update installs, the panel says **Updating**, **Keep it plugged in** and **Back in a minute** (on a single 64x64 panel, **Updating**, **Keep it plugged in**). Leave it powered. It restarts on the new version and comes back on its start-up screen, or goes back to what it was showing if the install doesn't go through. The device page shows how the update is going, and a bar at its top with **Install** whenever an update is out. See [Updates](/using/updates/).

On three or four panels in a row, Apollo, the Apollo Automation dog, sits at the left end, beside the words, while it updates. On two rows of two he sits in the middle of the top half, with the words under him. On 128x64 and 64x64 panels the words keep the whole screen, as above.

| The device page says | What it means |
| --- | --- |
| **Update available** and a version | A newer release is out, or a newer beta with **Follow beta** on. Press **Install** to install it. |
| **Installing** and a version | The update is running. Keep the panel powered. The page reconnects when the panel is back. |
| **You're up to date** | The panel has the latest release, or the latest beta with **Follow beta** on. |
| **You're ahead of the latest release** and a version | The panel runs a newer version than the latest release, like a beta. Nothing to install. Once the next release is out, the page offers it. |
| **You're ahead of the latest beta** and a version | With **Follow beta** on, the panel runs a newer version than the latest beta, like a release that came out after that beta. It waits for a beta of the next release. Nothing to install. |
| **The update to** a version **didn't take and the panel went back to** another | The new version crashed as it started, so the panel went back to the one it had. It won't install that version on its own again. **Install** tries it again. See [Troubleshooting](/help/troubleshooting/#the-device-page-says-an-update-didnt-take). |
| **Could not reach the update server. Try again in a minute.** | After you tap **Check for updates**, the check didn't get an answer. |
| **Not checked yet** | The panel hasn't checked for updates since it started. It checks a few seconds after its clock is set, or tap **Check for updates**. |
