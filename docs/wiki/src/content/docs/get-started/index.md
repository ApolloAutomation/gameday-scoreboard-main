---
title: "Get started"
description: "What Game Day Scoreboard is, what you need, and the three steps from the box to your team's score on the wall."
sidebar:
  order: 1
---

Game Day Scoreboard is an LED panel that shows your team's live score, game clock and next game. It follows the NFL, college football, MLB, the NBA, the WNBA, men's college basketball, the NHL, MLS and the Premier League. Once it's on your Wi-Fi it gets the scores from ESPN by itself, with no hub, account or other computer to run.

## What you need

- A panel running Game Day Scoreboard: the scoreboard75 controller board with its 128x64 LED panel and 5 V power supply, or an [Apollo Automation M-1 LED Matrix](https://apolloautomation.com/products/m-1-led-matrix) (Rev6).
- A 2.4 GHz Wi-Fi network and its password. The panel can't join a 5 GHz network.
- A phone or laptop with a web browser.

Built your own on a MoonHub75 board or another ESP32-S3 board, or putting the firmware on an Apollo M-1? Start with the [DIY pages](/diy/).

## From the box to your team

1. **Plug it in.** Connect the power supply. After a few seconds the panel shows **SETUP** and a QR code. See [Set up your panel](/get-started/setup/).
2. **Put it on your Wi-Fi.** Use the Wi-Fi dialog the installer offers right after an install, or join the panel's own **Game Day** hotspot from any phone or laptop. Both are on [Set up your panel](/get-started/setup/).
3. **Pick your team.** Choose it on the panel's own web page, the device page, or from [Home Assistant](/using/home-assistant/). See [Pick your team](/get-started/pick-your-team/).

That's all the setup there is. Before a game the panel shows the matchup and the start time. When the game starts it switches to the live board, celebrates scores, and stays on the final for a while before it looks up the next game.

If something doesn't go to plan, [Help](/help/) covers the common problems and what the panel's messages mean.
