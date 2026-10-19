---
title: "Troubleshooting"
description: "Fixes for the common Game Day Scoreboard problems, each starting from what you see on the panel, the app or the device page."
sidebar:
  order: 2
---

Find what you're seeing, then try the fix. Each entry ends with why it happens, in case the fix doesn't take.

## The app can't find my panel

The Game Day app is a separate project that Apollo Automation does not maintain, and it may not find panels running this firmware. Everything it sets is on the panel's own [device page](/using/device-page/), and its hotspot sets up Wi-Fi (see [Set up your panel](/get-started/setup/)).

To try the app anyway, check that your phone is on the same Wi-Fi network as the panel. Then add the panel by address: tap the add button in the app and enter the panel's name, like `gameday-2f6a70.local`, or its IP address. If you don't have either, hold the button on the controller for about 1.5 seconds and the panel shows both for 20 seconds.

A panel that isn't set up yet shows **SETUP**. The app finds that one over Bluetooth, so keep Bluetooth on and the phone near the panel.

**Why:** where the app works with this firmware, it finds a new panel over Bluetooth and a panel that's already set up over your Wi-Fi. A guest network, or a phone on mobile data, can keep the phone and the panel apart. Some phones won't open `.local` names, and the IP address works there.

## The panel won't join my Wi-Fi

- Pick the 2.4 GHz network. If your router has separate names for 2.4 GHz and 5 GHz, the panel can only use the 2.4 GHz one.
- Check the password.
- Use your main network, not a guest network.

While it joins a network from the Game Day app, the panel shows **JOINING** and the network's name, so a wrong pick shows up there.

If the panel joins but then says **Wi-Fi needs sign-in, use home or a hotspot**, with the QR code for the app, the network wants you to sign in on a web page, like many bar, hotel and dorm networks. The panel can't sign in to those pages. Set it up on a home network or a phone hotspot instead. The message goes away by itself once scores come through.

**Why:** the panel's Wi-Fi only works on 2.4 GHz. A guest network can let the panel join but keep your phone from reaching it afterwards. A network with a sign-in page lets the panel join, then holds back everything else until someone signs in on that page.

## The panel says Wi-Fi lost

If your router restarted or the internet dropped, wait. The panel rejoins by itself when the network comes back and the score returns.

If you changed your router or your Wi-Fi password, join the panel's **Game Day** hotspot once it comes up, within a few minutes, and give it the new network or password. The Game Day app can do the same where it finds the panel. See [New router or Wi-Fi password](/get-started/setup/#new-router-or-wi-fi-password).

**Why:** the panel says so when it has been off its network for more than about 10 seconds. It turns Bluetooth back on so the app can reach it, and restarts itself once, a minute into the outage.

## The panel says Can't reach ESPN

Check that other devices on the same network can get online. The panel keeps trying by itself and waits a little longer after each failed try. Once the internet is back, tap **Refresh scores** on the device page to try right away.

**Why:** the panel is on your Wi-Fi but can't get an answer from ESPN, where the scores come from. That's usually the internet connection, and sometimes ESPN itself.

## The panel says Getting ready for a long time

Check that other devices on the same network can get online. If they can, try another network, like your home Wi-Fi or a phone hotspot.

**Why:** the panel needs the time before it can read any scores. It asks a time server on the internet. Some guest, office and school networks block time servers, and then the panel sets its clock from ESPN instead, a few seconds after it joins. So **Getting ready** that lasts means the panel can reach neither. If it says **Wi-Fi needs sign-in** instead, see [The panel won't join my Wi-Fi](#the-panel-wont-join-my-wi-fi).

## Half the board is blank, or the picture shows twice

Open the device page and find **Matrix** in the Setup card. Click the picture that matches your panel:

| Your panel | Pick |
| --- | --- |
| The 128x64 panel from the Game Day Scoreboard, or two 64x64 panels side by side | **One 128x64 panel** |
| A single 64x64 panel | **One 64x64 panel** |

The panel restarts with the new layout.

**Why:** the panel's layout setting doesn't match the panel, so it draws the board for a different size. A 128x64 panel set to **One 64x64 panel**, for one, draws the picture twice side by side. See [Panel sizes](/using/panel-sizes/).

## Lines on the panel, or the wrong colors

1. Unplug the panel's power for 10 seconds and plug it back in. **Reboot** on the device page or in the app doesn't cut the power, so it won't clear this.
2. If the lines or colors come back, unplug the power and reseat the connection between the controller and the panel's input: the ribbon cable, or the controller itself where it plugs onto the panel. Push it in firmly and straight, then plug the power back in.
3. If they still come back, [open an issue on GitHub](/help/support/).

**Why:** this is almost always the connection between the controller and the panel, not the firmware. A HUB75 panel gets its top half and its bottom half over separate data lines. When a problem starts or stops exactly at the middle row, it follows that split. Colors tell you which line: text that should be white but shows purple is missing its green, since purple is red and blue without the green.

## The panel keeps losing Wi-Fi

Open the device page and find **Signal** under Diagnostics in the Device card. It's in dBm, and the closer the number is to zero, the stronger the signal. Under it, a graph shows the signal over the last day, or since the panel last started if that's more recent, with a dashed line at -75 dBm. When the signal has been below that line for most of the last half hour, the page says **Weak signal**. Move the panel closer to the router, or the router closer to the panel.

**Wi-Fi drops** counts how often the panel lost its network in that time, and says what caused the last drop and how long ago it was.

**Why:** a drop marked **signal lost** means the panel and the router stopped hearing each other, which is what a weak signal does. One marked **dropped by the router** means the router sent the panel away or restarted.

## I need to reset Wi-Fi

Use **Reset Wi-Fi** in the Device card on the device page, or the button on the controller if you can't reach the page. Nothing resets while you hold the button. Letting go does what the screen says:

| Hold for | The panel says | Let go and |
| --- | --- | --- |
| 1.5 seconds | Its address and IP address | Nothing resets |
| 5 seconds | **Keep holding, Wi-Fi reset in 5s**, counting down, with a bar filling | Nothing resets |
| 10 seconds | **Release to reset Wi-Fi**, with a countdown to erase all | Wi-Fi resets. The panel says **Wi-Fi reset, restarting** |
| 20 seconds | **Release to erase all**, with a countdown to cancel | Factory reset |
| 30 seconds | **Release to cancel** | Nothing resets |

Holding the button on a panel that's switched off turns it on, so you can see the countdown.

**Why:** a Wi-Fi reset restarts the panel into setup for 15 minutes, so you can put it on a new network from the Game Day app or its hotspot. It keeps its saved networks, and the setup screen says **Unplug it to rejoin saved Wi-Fi**. Unplug it and plug it back in and it rejoins the Wi-Fi it already knows. It does that by itself after the 15 minutes too, or a little later if you're still setting it up. Your team, favorites and other settings stay as they were. See [Buttons and resets](/using/buttons-and-resets/).

## Start over from scratch

Use **Factory reset** in the Device card on the device page. It erases everything: Wi-Fi, team, favorites and settings. The panel restarts into setup the way it came out of the box. Do this before you sell or give the panel away.

No app and no device page? Hold the button on the controller. At 20 seconds the panel says **Release to erase all**. Let go then for a factory reset. Changed your mind? Keep holding to 30 seconds, until it says **Release to cancel**, and let go.

A [USB install](/get-started/install-firmware/) erases everything too.

## The panel won't start

If it stays dark or keeps restarting, check that it's on the power supply that came with it. If that doesn't help, reinstall the firmware over USB from a desktop browser such as Chrome or Edge. See [Install or reinstall the firmware](/get-started/install-firmware/). That erases its settings, so set it up again afterwards.

**Why:** a USB install replaces everything on the controller, so it brings back a panel that can't start the firmware it has.

## The panel is dim, or shows a thermometer or plug in the corner

A small icon pulsing slowly in the top right corner means the panel is holding its brightness down to protect itself. Every two minutes the bottom line says why, for 15 seconds.

**An orange thermometer, and Too warm, dimming:** the panel is running hot. Keep it out of direct sun and give it some air behind it. It goes back to your brightness by itself as it cools.

**A red plug, and Check power supply:** the panel restarted twice in a row because its power dipped, so it keeps its brightness low to stay on. Use the power supply that came with it, not a phone charger or a USB port. Then press **Clear** on the device page. The icon stays until you do.

**Why:** above a 75 C reading from the controller's chip, the panel steps its brightness down, a step every two minutes while it stays hot, as far as a quarter of your setting. Once the chip reads under 70 C, it comes back a step at a time. A power supply too weak for the panel makes it restart when it lights up, and a lower brightness draws less power. The device page says the same under the board, and shows the chip temperature under Diagnostics. See [Heat and power protection](/using/brightness-and-clock/#heat-and-power-protection).

## The device page says an update didn't take

The Firmware box on the device page says **The update to** the new version **didn't take and the panel went back to** the version it's running, with **Install tries it again.** under it. With **Install updates automatically** on, it adds **It won't install this one on its own.**

Nothing needs doing. The panel is running the version it had before, and it works as it did.

If you want to try the update again, press **Install**. A newer version installs as normal.

**Why:** when a new version crashes as it starts, the panel goes back to the firmware it had. It remembers that version and doesn't install it on its own again, so it doesn't restart into the same bad update every night. See [Updates](/using/updates/#an-update-that-didnt-take).
