---
title: "Get started"
description: "What Apollo Pulse is, what you need, and the three steps from the box to your team's score on the wall."
sidebar:
  order: 1
---

Apollo Pulse is an LED panel that shows your team's live score, game clock and next game. It follows the NFL, college football, MLB, minor league baseball, the NBA, the WNBA, men's college basketball, the NHL, the AHL, the ECHL, MLS and the Premier League, and golf's PGA Tour and LPGA Tour and racing's Formula 1 and NASCAR Cup Series. Once it's on your Wi-Fi it gets the scores by itself, from ESPN, and for minor league baseball from MLB's stats service and for the AHL and the ECHL from HockeyTech, with no hub, account or other computer to run.

It follows golf and racing too. Pick the PGA Tour or the LPGA Tour and the panel shows that tour's tournament and leaderboard. See [Golf](/using/golf/). Pick Formula 1 or the NASCAR Cup Series and it shows the race weekend, the order of each session and the race's winner. See [Racing](/using/racing/).

It isn't only for games. Under **Showing** on the device page, the panel can show [Weather](/using/weather/) at your home, the [Flight Tracker](/using/flights/) with the planes flying nearby, [Stocks & Crypto](/using/markets/) with up to eight prices, or the [F1](/using/f1/) display, in place of the Game Day board. On three or four panels in a row or two rows of two, one of them can sit in a [second area](/using/panel-sizes/#the-second-area) beside it. See [Showing](/using/brightness-and-clock/#showing-the-scoreboard-the-clock-or-another-display).

Apollo Pulse used to be called Game Day Scoreboard. Home Assistant still lists the panel by that name.

## What you need

- A panel running Apollo Pulse: the scoreboard75 controller board with its 128x64 LED panel and 5 V power supply, or an [Apollo Automation M-1 LED Matrix](https://apolloautomation.com/products/m-1-led-matrix) (Rev6).
- A 2.4 GHz Wi-Fi network and its password. The panel can't join a 5 GHz network.
- A phone or laptop with a web browser.

Built your own on a MoonHub75 board or another ESP32-S3 board, or putting the firmware on an Apollo M-1? Start with the [DIY pages](/diy/).

## From the box to your team

1. **Plug it in.** Connect the power supply. After a few seconds the panel shows **SETUP** and a QR code that joins its own **Apollo Pulse** hotspot. See [Set up your panel](/get-started/setup/).
2. **Put it on your Wi-Fi.** Point your phone's camera at the QR code to join the hotspot, or join it from any phone or laptop, and pick your network on the page that opens. Right after an install, the installer's Wi-Fi dialog does it too. Both are on [Set up your panel](/get-started/setup/).
3. **Pick your team.** Once the panel is on your Wi-Fi, its QR code opens its own web page, the device page. Choose your team there, or from [Home Assistant](/using/home-assistant/). See [Pick your team](/get-started/pick-your-team/).

The hotspot is named **Apollo Pulse** plus six letters and numbers, like `Apollo Pulse 2f6a70`. The panel's start-up and setup screens say **APOLLO PULSE**, and the device page's heading and browser tab say **Apollo Pulse**.

That's all the setup there is, and there's no app to install. Before a game the panel shows the matchup and the start time. When the game starts it switches to the live board, celebrates scores, and stays on the final for a while before it looks up the next game.

If something doesn't go to plan, [Help](/help/) covers the common problems and what the panel's messages mean.
