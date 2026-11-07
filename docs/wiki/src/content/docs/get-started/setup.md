---
title: "Set up your panel"
description: "Plug in the panel, put it on your Wi-Fi from the installer or its own hotspot, and find its device page or add it to Home Assistant."
sidebar:
  order: 2
---

You need a phone or computer with a web browser, and your Wi-Fi password. It takes a few minutes.

## Plug it in

Connect the power supply that came with the panel. The panel lights up with **GAME DAY** across the top and **Starting** under it.

A panel that isn't on Wi-Fi yet then shows **SETUP** and starts its own Wi-Fi hotspot. In the moment before the hotspot is up, it says **SETUP / Starting / hotspot**, with no QR code. Then a QR code appears: it's the hotspot's Wi-Fi code, and the words say **SETUP / Scan to / join Wi-Fi**. On the 128x64 panel the QR code sits on the left half and the words sit beside it. On a single 64x64 panel there is no room for both, so the words and the QR code take turns: about 5 seconds of words, then 8 seconds of QR code.

Point the camera of any iPhone or Android phone at the QR code and tap to join, then pick your Wi-Fi network on the setup page, as below. Your phone's camera and browser are all you need, with no app to install. The installer's Wi-Fi dialog sets the panel up too.

## Put it on Wi-Fi from the installer

If you just installed the firmware from your browser, the installer offers to put the panel on your Wi-Fi as soon as the install finishes. Enter your network name and password in that dialog and the panel joins. Then [pick your team](/get-started/pick-your-team/) on its device page.

## Use the panel's hotspot

While it shows SETUP, the panel also starts its own Wi-Fi network, named **Game Day** plus six letters and numbers, like `Game Day 2f6a70`. Once the hotspot is up, the panel takes a turn showing **Join Wi-Fi / Game Day** and those six characters, so you know which network is yours. Two panels in one house each get their own name.

1. Join that network. On a phone, point the camera at the QR code on the panel and tap to join; on a 64x64 panel, wait for the code's turn. Or pick the network from the Wi-Fi list on any phone or laptop. A setup page opens. If it doesn't, tap your phone's notification to sign in to the network, or open `192.168.4.1` in a browser. If your phone says the network has no internet, choose to stay connected.
2. Pick your Wi-Fi network and enter the password.
3. Save.

The panel joins your Wi-Fi. Then [pick your team](/get-started/pick-your-team/) on its device page.

## Pick your team, or add it to Home Assistant

Once the panel is on your Wi-Fi, its own web page, the device page, holds every setting. To open it, scan the QR code the panel shows with your phone's camera, or open the address it shows in a browser (both below). Your phone or computer has to be on the same Wi-Fi. See [The device page](/using/device-page/).

If you run Home Assistant, it finds the panel as a new ESPHome device, and picking a team or a mode there sets the panel up too. See [Home Assistant](/using/home-assistant/).

## The Game Day app

The Game Day app is a separate phone app that Apollo Automation does not maintain. The panel doesn't show a QR code for it or send you to it, and you don't need it. It may not find panels running this firmware, so use the steps above if it doesn't. Where it does find a new panel, you can set it up over Bluetooth. The app lists it as a new panel nearby:

1. Tap the panel to set it up.
2. Pick your Wi-Fi network from the list. The list comes from the panel itself, strongest network first, so it shows what the panel can actually reach.
3. Enter the password.

While the panel joins, it shows **JOINING** and the name of the network the app sent it. If that's the wrong network, you can see it right there. Once the panel is on your Wi-Fi, the app lets you pick your team.

## Use 2.4 GHz Wi-Fi

The panel only joins 2.4 GHz networks. If your router has separate names for 2.4 GHz and 5 GHz, pick the 2.4 GHz one.

Use your main network, not a guest network. A guest network can let the panel join but keep your phone from reaching it afterwards.

Some bar, hotel and dorm Wi-Fi lets the panel join and then wants a sign-in on a web page before anything else gets through. The panel can't sign in to those pages. On a network like that it says **Wi-Fi needs / sign-in / use home or / a hotspot** with a QR code that opens the panel's device page by its IP address. Set it up on a home network or a phone hotspot instead. [Reset Wi-Fi](/using/buttons-and-resets/#reset-wi-fi) puts it back into setup so you can: scan the QR code from a phone on the same Wi-Fi and tap **Reset Wi-Fi** in the Device card. Some of these networks keep devices from reaching each other, and then the page won't open; hold the button on the controller and let go between 10 and 20 seconds to reset Wi-Fi instead. The message goes away by itself once scores come through.

## What the panel shows once it's online

Right after it joins your Wi-Fi, the panel shows a QR code that opens its own device page, at its IP address, like `http://192.168.1.50`. Point your phone's camera at it and tap the link to pick your team, with no app and no `.local` name to type. Your phone has to be on the same Wi-Fi as the panel. On the 128x64 panel the words **Scan to / pick your / team** sit beside the code.

After about 13 seconds the panel's address takes a 5 second turn: **Open gameday-2f6a70.local**, with your panel's own six characters. That address is the device page too, by name. The six characters are the same ones in the hotspot's name. Open the address in a browser on any phone or computer on the same Wi-Fi to pick your team and change settings. See [The device page](/using/device-page/).

A new panel keeps the QR code and the address up for 45 seconds, or until you pick a team. A panel that already has a team shows them once, for about 18 seconds, every time it starts. Then it says **Looking up scores** while the first scoreboard loads, for up to 10 seconds.

Until you pick a team or a mode, the ticker along the bottom says **Pick a team at** followed by the panel's IP address, like `192.168.1.50`. Use the IP address if your phone won't open a `.local` address.

To see the address again any time, hold the button on the controller for about 1.5 seconds and let go. The panel shows its full address and IP address for 20 seconds. More on the button in [Buttons and resets](/using/buttons-and-resets/).

:::tip
To skip the QR code and the address when the panel starts, turn off **Show how to connect when the panel starts** in the Setup card on the device page. A panel that has a team then goes straight to the scoreboard. A new panel still shows the QR code, without the address, so you can pick one.
:::

## New router or Wi-Fi password

When the panel loses its Wi-Fi for more than about 10 seconds, it covers the score and says **Wi-Fi / lost / reconnecting**, with no QR code, while it tries to get back on. A shorter drop, like a mesh system moving the panel to another access point, never covers the score. If the router was only down, there's nothing to do: the panel rejoins by itself once it's back.

For a new router or password, use the panel's **Game Day** hotspot. It comes up within a few minutes of the panel losing Wi-Fi, and then the panel says **Wi-Fi / lost / scan to fix** with the hotspot's QR code, and takes turns showing the hotspot's name. Scan the code with your phone's camera, or join the hotspot as above, and give the panel the new network or password on the page that opens.

If you use the Game Day app: about 30 seconds into the outage the panel turns Bluetooth back on, so the app can find it, and you give it the new network or password the same way as the first time. If the app hands the panel a wrong password while it's in an outage, the panel goes back to the network it had.

A panel that can't get back on its Wi-Fi restarts itself once, a minute into the outage. It never does that while you're connected to it from the app.

## The panel remembers up to five networks

Every network you set up is added to a list of up to five. Set the panel up on a phone hotspot for a tailgate, and it still knows your home network when it gets back. When it starts, it joins whichever saved network it can see, trying the last one that worked first.

The Device card on the device page lists the saved networks under **Wi-Fi networks**. **Connected** marks the one in use, and **Tried first** marks the one it tries first. **Forget** takes a network off the list.

[Reset Wi-Fi](/using/buttons-and-resets/#reset-wi-fi) keeps the saved networks. The panel restarts into setup for 15 minutes so you can give it a new network. While it's in setup, the panel takes a turn saying **Unplug it to / rejoin / saved Wi-Fi**. If you unplug it and plug it back in, or the 15 minutes run out, it goes back to the networks it already knows. Only [Factory reset](/using/buttons-and-resets/#factory-reset) clears the list, along with everything else. See [Wi-Fi](/using/wifi/) for more.
