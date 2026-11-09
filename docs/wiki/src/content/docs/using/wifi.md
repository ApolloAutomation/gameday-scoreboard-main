---
title: "Wi-Fi"
description: "The networks the panel joins, the up to five it remembers, its setup hotspot, what it does when Wi-Fi drops, and its .local address."
sidebar:
  order: 15
---

The panel needs Wi-Fi to read scores from ESPN. Once it's on your network, it needs nothing else running.

## Use 2.4 GHz

The panel only joins 2.4 GHz networks. If your router has separate names for 2.4 GHz and 5 GHz, pick the 2.4 GHz one.

Use your main network, not a guest network. A guest network can let the panel join but keep your phone from reaching it afterwards, so the device page won't open.

Some bar, hotel and dorm Wi-Fi lets the panel join and then wants you to sign in on a web page before anything else gets through. The panel can't sign in to those pages. On a network like that it covers the board with **Wi-Fi needs / sign-in / use home or / a hotspot** and a QR code, and the device page says **Wi-Fi needs a sign-in**. Put the panel on a home network or a phone hotspot instead. [Reset Wi-Fi](/using/buttons-and-resets/#reset-wi-fi) puts it back into setup so you can. Scan the QR code from a phone on the same Wi-Fi: it opens the panel's device page by its IP address, where **Reset Wi-Fi** in the Device card does that. Some of these networks keep devices from reaching each other, and then the page won't open; use the button's Reset Wi-Fi instead. The message goes away by itself once scores come through.

## Saved networks

The panel remembers up to five Wi-Fi networks, like your home, a phone hotspot and a friend's house. At start-up it joins whichever saved network it can see, trying the last one that worked first. Set it up on a phone hotspot for a tailgate and it still knows your home network when you get back.

The Device card on the device page lists the saved networks under **Wi-Fi networks**, in the order the panel tries them, with **Connected** beside the one it's on, or **Tried first** beside the top one. **Forget** takes a network off the list. If the panel is on that network now, it stays connected until that connection drops or the panel restarts, and after that it doesn't join that network again, unless it was the only one on the list (see below).

When the list is full, adding a sixth network drops the one at the bottom. The panel always keeps one network to join, so forgetting the last one doesn't take it off Wi-Fi: the panel keeps joining that network and puts it back on the list the next time it connects. Only [Factory reset](/using/buttons-and-resets/#factory-reset) clears them all.

[Reset Wi-Fi](/using/buttons-and-resets/#reset-wi-fi) keeps the saved networks. The panel restarts into setup for 15 minutes so you can add a network, and takes a turn saying **Unplug it to / rejoin / saved Wi-Fi**. Unplug it and plug it back in, or let the 15 minutes run out, and it goes back to the saved networks. If you're still connected to its hotspot or the app when the time is up, it waits a little longer.

## Adding a network

Every network you set the panel up on is added to the top of the list. There are three ways:

- **The panel's hotspot.** While it shows **SETUP**, the panel starts its own Wi-Fi network named **Game Day** and six letters and numbers, like **Game Day 2f6a70**. Once the hotspot is up, the screen says **SETUP / Scan to / join Wi-Fi** with a QR code, the hotspot's Wi-Fi code: point your phone's camera at it and tap to join, instead of looking for the network in your phone's list. The screen also takes turns showing the hotspot's name, so you can join it from any phone or laptop. A setup page opens. Pick your Wi-Fi network, enter the password and save.
- **The installer.** Right after you install the firmware from your browser, the installer offers to put the panel on your Wi-Fi. Enter your network name and password there. See [Set up your panel](/get-started/setup/#put-it-on-wi-fi-from-the-installer).
- **The Game Day app**, a separate project that may not find panels running this firmware, finds the panel over Bluetooth where it works and hands it the network and password. While the panel joins, it shows **JOINING** and the network's name. See [The Game Day app](/get-started/the-app/).

The six characters are the same as in the panel's address, so two panels in one house are easy to tell apart. See [Set up your panel](/get-started/setup/).

Taking the panel somewhere new? When it can't find any saved network, it says **Wi-Fi lost** and, within a few minutes, brings up its hotspot, as below, so you can give it the new network there. To put it into setup straight away, use [Reset Wi-Fi](/using/buttons-and-resets/#reset-wi-fi).

## When Wi-Fi drops

A short drop, like a mesh system moving the panel to another access point, doesn't show. After 10 seconds without Wi-Fi the panel covers the board with **Wi-Fi / lost / reconnecting**, with no QR code, so a stale score isn't left up. It keeps trying its saved networks the whole time.

- Within a few minutes the panel's **Game Day** hotspot comes up, and the screen says **Wi-Fi / lost / scan to fix** with the hotspot's QR code, taking turns with the hotspot's name. Scan the code with your phone's camera to join the hotspot, or join it by name, and give the panel the new network or password. A changed router password is the common case.
- After 30 seconds, the panel turns Bluetooth back on so the Game Day app can reach it, where the app works with this firmware.
- After a minute without its network, a panel that was on it earlier restarts itself once, to recover. It never restarts while the app is connected to it.

When the network comes back, the panel rejoins and the board comes back by itself.

To see how often it happens, look under **Diagnostics** in the Device card on the device page. Under **Signal** the page draws the last day of Wi-Fi signal, says when the signal is weak, and counts the **Wi-Fi drops** of the last day with the cause of the last one. See [Diagnostics](/using/device-page/#diagnostics).

## The panel's address

Every panel has a name on your network, `gameday-` and six characters, like `gameday-2f6a70`. Its device page is at that name plus `.local`, like `gameday-2f6a70.local`. The panel shows the address when it joins Wi-Fi, and again when you hold the boot button for about 1.5 seconds. The device page shows it in the Device card under **Address** and **Name**.

The `.local` name and the IP address, a number like `192.168.1.50`, open the same device page. The name never changes and is different on every panel, so two panels in one house never get mixed up. The IP address comes from your router, which can hand the panel a different one later. Some phones can't open `.local` addresses: use the IP address there. To find it:

- **The QR code** the panel shows when it joins Wi-Fi opens its device page by IP address, like `http://192.168.1.50`, so a phone that can't open `.local` addresses gets there with its camera.
- **The boot button**: hold it for about 1.5 seconds and the panel shows its `.local` name and IP address for 20 seconds.
- **The ticker** starts with the IP address and the `.local` name for 30 seconds after the panel joins Wi-Fi, like **IP 192.168.1.50 | gameday-2f6a70.local**, once the QR code and the address go, or straight away when a game is live. It follows **Show how to connect when the panel starts**. See [What the panel shows once it's online](/get-started/setup/#what-the-panel-shows-once-its-online). Until you pick a team, it also says **Pick a team at** and the IP address.
- **The device page** shows it in the Device card under **Address**, with the `.local` name under **Name**.
- **Home Assistant** has it as the panel's **IP** entity, and your router's list of connected devices shows it beside the panel's name, `gameday-` and its six characters.

The panel treats your home network as trusted. Anyone on your Wi-Fi can open its device page and change its settings, and it isn't designed to be reached from the internet, so don't forward a port to it.
