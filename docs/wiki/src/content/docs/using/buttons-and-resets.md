---
title: "The boot button and resets"
description: "What a press or a hold of the controller's button does, and what Reset Wi-Fi and Factory reset keep and clear."
sidebar:
  order: 14
---

The controller has one button, the boot button. A quick press turns the panel on or off. Holding it shows the panel's address, resets its Wi-Fi or erases everything, so you can get back in without the app or the device page.

## Presses and holds

Nothing resets while you hold the button. The screen says what letting go will do, and letting go does it. Hold it and watch the screen:

| Hold | The screen says | Let go then and |
| --- | --- | --- |
| About 1.5 seconds | The panel's `.local` address and its IP address, when it's on Wi-Fi | nothing is reset. The address stays up for 20 seconds, then the panel goes back to the board. |
| 5 to 10 seconds | **Keep holding / Wi-Fi reset / in 5s**, counting down, with a bar filling | nothing is reset. |
| 10 to 20 seconds | **Release to / reset Wi-Fi / Hold 10s to / erase all**, counting down | the panel says **Wi-Fi reset / restarting** and resets Wi-Fi. See [Reset Wi-Fi](#reset-wi-fi). |
| 20 to 30 seconds | **Release to / erase all / Hold 10s to / cancel**, counting down | the panel says **Erasing all / restarting** and does a factory reset. See [Factory reset](#factory-reset). |
| Past 30 seconds | **Release to / cancel** | nothing is reset. |

Holding the button on a panel that is off turns it on, so you can see the countdown.

The address hold is the quickest way to find the panel again. See [Wi-Fi](/using/wifi/).

## Reset Wi-Fi

Reset Wi-Fi is for a new router, a new Wi-Fi password, or moving the panel to another network. Start it by letting go of the button between 10 and 20 seconds, or with **Reset Wi-Fi** in the Device card on the device page. The page asks you to confirm first.

The panel keeps the Wi-Fi networks it has saved and restarts into setup for 15 minutes. It shows **SETUP** and the QR code for the Game Day app, and starts its own hotspot. Put it on the new network from the app or the hotspot; see [Set up your panel](/get-started/setup/). The new network goes to the top of the saved list.

The setup screen also takes a turn saying **Unplug it to / rejoin / saved Wi-Fi**. Unplug the panel and plug it back in, and it goes back to the networks it already knows. It does that by itself after the 15 minutes too, or up to 10 minutes later if you're still setting it up from the app or the hotspot.

Your team, favorites and every other setting stay as they were. To clear the saved networks, use [Factory reset](#factory-reset). To drop one network, use **Forget** on the device page; see [Wi-Fi](/using/wifi/#saved-networks).

## Factory reset

Factory reset erases everything: the saved Wi-Fi networks, your team, your favorites and every setting, including a paired WizMote and the panel layout. The panel restarts into setup the way it came out of the box, with the layout its firmware starts on. Do this before you sell or give the panel away.

Start it from **Factory reset** in the Device card on the device page. The page asks you to confirm first.

Without the device page, hold the boot button until the screen says **Release to / erase all**, at 20 seconds, then let go. The panel says **Erasing all / restarting**. If you change your mind, keep holding past 30 seconds, until it says **Release to / cancel**, then let go.

## What each reset keeps

| | Reset Wi-Fi | Factory reset |
| --- | --- | --- |
| Saved Wi-Fi networks | Kept | Cleared |
| Team, mode and favorites | Kept | Cleared |
| Settings (ticker, brightness, timezone and the rest) | Kept | Cleared |
| WizMote pairing | Kept | Cleared |
| Panel layout | Kept | Back to the firmware's starting layout |
| Firmware version | Kept | Kept |

If the panel won't start at all, a reset can't help. See [Troubleshooting](/help/troubleshooting/).
