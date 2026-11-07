---
title: "Racing: Formula 1 and NASCAR Cup Series"
description: "Follow Formula 1 or the NASCAR Cup Series like a team: the race weekend's sessions and the latest one's order, the race winner, the next race between weekends, and a clock between seasons."
sidebar:
  order: 9.7
---

:::note[Coming in the next release]
Racing is new in the next release. Everything on this page describes it; the current release has no Racing tab.
:::

You follow a racing series the way you follow a team or a golf tour. Pick **Formula 1** or the **NASCAR Cup Series**, and the panel shows that series' race weekend: the next session and when it starts, the order of the session on track or the latest one, and the race's winner. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/). The names and gaps in its examples show the format, not real results.

## Following a series

:::note[Coming in the next release]
On the device page, tap **Change team** and open the **Racing** tab, which holds both series. Tap **Formula 1** or **NASCAR Cup Series**. Search finds them too, by their names or by "racing". In Home Assistant, the **Team** entity has **Racing: Formula 1** and **Racing: NASCAR Cup Series** at the end of its list.

The series is your team in **My team**. In **Live games**, when nothing is live, the panel shows your series the way it would show your team, with "No live NFL games, showing F1" at the front of the ticker. See [Show: My team, Favorite teams, Live games](/using/modes/#when-nothing-is-live).
:::

## Before a race weekend

:::note[Coming in the next release]
Once ESPN lists the weekend, the board shows the race's name and the first session with its day and start time in the panel's timezone, like "Sao Paulo GP" over "FP1 Fri 10:30a", or "Miami" over "Race Sun 3:00p" for NASCAR. Names are shortened to fit: an F1 race keeps its place and "GP" without the sponsor ("US GP", "Las Vegas GP"), and a NASCAR race drops "NASCAR Cup Series at". Where even that doesn't fit, Circuit of the Americas shows as "COTA" and North Wilkesboro as "Wilkesboro". Before ESPN lists the weekend, the board shows the race's name and its days, like "Las Vegas GP" over "Nov 19-21".

The sessions are named the way the board shows them: **FP1** to **FP3** for practice, **Qual** for qualifying, **Sprint Q** for the sprint shootout (**SQ** in the top row, like "SQ live"), **Sprint** and **Race**. The ticker spells them out, like "Practice 1" and "Sprint qualifying", and adds the track and, when **Pre-game odds and TV** is on, the TV channel.
:::

## During a session

:::note[Coming in the next release]
The top row says where the session is, in green: "Lap 34/56" during the race, "Sprint 5/19" during a sprint, or "FP3 live", "Qual live" and "SQ live" in practice and qualifying. A red flag shows "Red flag" in red. A yellow flag or a caution turns the lap count amber, and a stopped or held-up session says "Suspended" or "Delayed" in amber. On a 128x64 display the race's name, shortened to fit, is on the left of it.

Under it are the first six places:

- **Place**, in gray.
- **Last name**: up to 10 letters on a single panel and 11 on a 128x64 display. A long name loses its suffix or its first small word before it's cut, so "Stenhouse Jr." shows as "Stenhouse" and "van Gisbergen" as "Gisbergen". Nothing on the board scrolls but the ticker. The ticker, the device page and Home Assistant have the whole name.
- **Car number** and **gap**, on a 128x64 display only. In practice and qualifying the leader shows their lap time, like "1:10.7", and everyone else how far behind it they are, like "+0.253". In a race the gap is to the leader, like "+13.07", or the laps down, like "+1L". A driver out of the race shows "DNF", and a disqualified one "DSQ".

The ticker has the session and the top 10 with their full gaps, like "1 Antonelli 1:10.763, 2 Norris +0.253" or "8 Wallace +1 lap", then the TV channel when **Pre-game odds and TV** is on.

With **Install updates automatically** on, an overnight update waits while a session is on track, through a red flag or a short stop too, but not for a race still stopped 6 hours after its start. See [Install updates automatically](/using/updates/#install-updates-automatically).
:::

## Between sessions

:::note[Coming in the next release]
When a session ends, its order stays on the board until the next session starts, with the top row saying so, like "FP2 final", "Qual final" or "SQ final". Qualifying's order stays up until the race starts. The ticker says what's next and when, like "Next: Practice 3, Today 2:30p".

A sprint's result shows "Sprint final". Only the race gets the winner's gold and the celebration.
:::

## The race final

:::note[Coming in the next release]
The top row says "Final" in gold and the winner's name turns gold. When the panel sees the race end, the winner's name fills the screen with fireworks for 4 seconds, like "ANTONELLI WINS!". On a single panel a name longer than 10 letters is cut to fit its line. The ticker says who won, then places 2 to 10 and, once known, the next race with its days.

The final stays up for a day after the race's start, or until ESPN moves on to the next race if that's sooner. Then the board shows the next race's name and days, like "Mexico City GP" over "Oct 30-Nov 1". If the panel can't read the series' schedule after a final, the final stays up and the panel tries again within minutes, rather than saying there's no race to come.
:::

## Canceled or postponed races

:::note[Coming in the next release]
A canceled or postponed race, or a whole canceled weekend, shows its name and "Canceled" or "Postponed". It stays up for a day after the race's planned start, then the board moves on to the next race.
:::

## Between races and seasons

:::note[Coming in the next release]
Between race weekends the board shows the next race's name and days. When the next race is more than 45 days away, the board shows the off-season clock, in red. On a 128x64 display the next race and its days sit under the time, like "Next: Australian GP" over "Mar 5-7", and on a single panel the date does. The ticker says "Next:" with the race's full name and days.

After a season's last race, until ESPN publishes the next season's schedule, the clock's ticker says "No upcoming Formula 1 race" or "No upcoming NASCAR Cup Series race". When this release was written, ESPN had no 2027 schedule yet for either series, so a panel following NASCAR shows the clock from the day after its November 8 finale, and one following Formula 1 from the day after the Abu Dhabi race on December 6. If ESPN lists nothing for the series at all, the board says "No upcoming race". See [Brightness, panel on and off, and the clock](/using/brightness-and-clock/).
:::

## How often it updates

:::note[Coming in the next release]
Every 30 seconds while a race or sprint is on track, and every minute during practice and qualifying and from 10 minutes before a session until ESPN says it has started. A suspended or delayed session is checked every 2 minutes, and every 15 once it's still stopped 6 hours after its start. Every 5 minutes in the 2 hours before a session, every 30 minutes the rest of the season, and every 6 hours while the off-season clock is up. A session ESPN still hasn't started 6 hours after its time is checked every 15 minutes. **Refresh scores** on the device page reads it again right away.

Each read is ESPN's summary of the weekend: up to about 95 KB during an F1 weekend, when every session's order is in it, and 3 to 27 KB for NASCAR.
:::

## On the device page and in Home Assistant

:::note[Coming in the next release]
The board on the device page shows the session's order too: the session and up to 10 rows with the place, the name, the car number and the gap. Before and between sessions it shows the next session and when it starts, and after the race the next race weekend.

Home Assistant's **Game Status** reads like "MSC Cruises Sao Paulo Grand Prix | Practice 2 final | Fastest: Antonelli", "MSC Cruises United States Grand Prix | Race | Leader: Antonelli", "MSC Cruises United States Grand Prix | Final | Antonelli wins" or "NASCAR Cup Series at Miami | Race Sun 3:00p". It changes when the leader or the session changes and at the final, not on every lap. **Last Play** stays empty for racing. See [Home Assistant](/using/home-assistant/).
:::

## NASCAR

:::note[Coming in the next release]
ESPN lists only the race for the NASCAR Cup Series, not its practice or qualifying, so the board goes from the race's card to the race. ESPN gives NASCAR no time gaps: a driver on the lead lap has an empty gap column, and one a lap or more down shows "+1L". ESPN can be slow to update a NASCAR race while it runs, so the board may keep showing the race's card for a while after the start.
:::

## Not there yet

:::note[Coming in the next release]
This first version follows a series, not a driver. Not there yet:

- Following one driver or team.
- A series as a favorite. The **Racing** tab is hidden while you add a favorite. See [Favorite teams](/using/favorites/).
- Racing in **Live games**. Its league buttons and the **On now** tab leave racing out.
- Other series, like NASCAR's O'Reilly Auto Parts Series (formerly the Xfinity Series), the Truck Series or IndyCar.
- Standings, NASCAR's stages and caution counts.
- F1's three-letter driver codes, which ESPN doesn't give. The board uses last names.
:::
