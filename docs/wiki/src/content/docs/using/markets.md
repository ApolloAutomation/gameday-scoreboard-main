---
title: "Stocks & Crypto"
description: "Up to eight stocks, funds, indexes and coins on the panel, with the price, the day's move in green or red, a chart of the day and a ticker, from Yahoo Finance with nothing to sign up for."
sidebar:
  order: 10.7
---

**Stocks & Crypto** is a display the panel can show in place of the scoreboard, or in the second area beside it: up to eight stocks, funds, indexes or coins, each with its price and how far it's up or down today in green or red, a chart of the day for one of them at a time, and a ticker with every one. The prices come from Yahoo Finance, which needs no account, key or sign-up. When Yahoo Finance can't be reached, the panel reads coins from Kraken and US stocks from Nasdaq, which need none either.

## Showing it

- **On the main board**: in the **Panel** card on the [device page](/using/device-page/), set **Showing** to **Stocks & Crypto**. **Scoreboard** brings your games back.
- **In the second area**, on three or four panels in a row or two rows of two: set **Shows** in the Quick settings card to **Stocks & Crypto**. While the main board shows Stocks & Crypto, **Shows** offers **Game Day** too, which puts the scoreboard in the second area instead. See [The second area](/using/panel-sizes/#the-second-area).

The panel keeps following your games while it shows Stocks & Crypto, and Home Assistant's Game Status and Last Play stay on them. A celebration shows only where the scoreboard is on screen.

## What the panel shows

Each symbol gets a row: the symbol, then its price and the day's change in percent, green when it's up, red when it's down and white when it hasn't moved. A row goes gray when its price is more than half an hour old while its market is open: a stock nobody has traded lately, or any symbol while the panel can't get new prices. Coins trade around the clock, so theirs is always open. A symbol the panel has no price for yet shows **--**.

| Board | Each row |
| --- | --- |
| One 64x64 panel, or a three-wide second area | The symbol, then the price for 4 seconds and the change in percent for 4. |
| 128x64, two rows of two, or a four-wide second area | The symbol, the price and the change in percent. |
| Three or four panels in a row, with no second area | The symbol, the price, the change and the change in percent. |

**The top row.** On 128 wide and up, **STOCKS**, **CRYPTO** or **MARKETS**, for what you've listed, with the page when there's more than one, like **MARKETS 1/2**. At the right is where the market stands: **OPEN** in green, or in gray **PRE-MARKET**, **AFTER HOURS**, **CLOSED**, or the day of the last close, like **FRI CLOSE**, over a weekend or a holiday. One panel has room for one word in the middle: **OPEN**, **PRE**, **AH**, **CLOSED** or the day, like **FRI**, or **CRYPTO** when you've listed only coins. With stocks from more than one market, the open one wins.

**The chart.** A line of one symbol's day so far, in its green or red, under the rows or, on four panels in a row, beside them. It moves on to the next symbol every 8 seconds; that symbol's name is white and the others gray. Under the chart there's room for four rows, so with more symbols the rows turn to the page with the charted one. Four panels in a row have room for six. A stock's chart runs over its trading day, 9:30 AM to 4 PM Eastern for a US stock, and a coin's from midnight UTC. Until the panel has a chart, the rows fill the area, six to a page, and the pages turn every 8 seconds.

**The ticker.** Every symbol with its price, change and percent, the change in green or red, like **AAPL 355.63 +7.12 +2.04% | BTC 73,738.65 -186.54 -0.25%**.

**Big and small numbers.** A price too long for its column loses its commas, then its cents, then shortens to thousands, like **73,738.65**, **73738.65**, **73,739** or **74K**. A price under a dollar shows up to four digits after the zeros, like **0.06726**, fewer when its column is narrow, and a price too small for its column shows as a power of ten, like **4.3e-6** for 0.00000435.

**Which day.** A stock's change is from the last close. A coin's is from midnight UTC, the way Yahoo Finance and Kraken count a coin's day, so it starts near zero then. Before the open and after the close the panel shows the regular trading day's price: it doesn't show pre-market or after-hours trades.

## Choosing the symbols

The **Stocks & Crypto** card on the device page holds the list, up to eight, in the order the panel shows them. A new panel starts with **SPY**, **AAPL**, **BTC** and **ETH**; the card says **the starting list** beside its name until you change it, and removing every symbol brings those four back.

To add one, type it under **Symbol** and tap **Add symbol**:

| Kind | Type it like |
| --- | --- |
| A stock or fund | **AAPL**, **SPY**, **QQQ**. A class share takes a dot or a slash, like **BRK.B** or **BRK/B**. A stock on another exchange takes Yahoo Finance's ending, like **SHOP.TO**. Where the board's symbol column is narrow, it shows the symbol without its ending, like **SHOP**. |
| An index | A caret, like **^GSPC** (the S&P 500), **^DJI** (the Dow), **^IXIC** (the Nasdaq Composite). **SPX**, **DJI**, **NDX**, **RUT** and **VIX** on their own are the indexes too. Two names for one index, like **SPX** and **^GSPC**, are the same symbol, so it's listed once. |
| A coin, in US dollars | **BTC**, **ETH**, **DOGE**, or any coin Yahoo Finance has as its symbol and **-USD**, like **ARB-USD**. |

**Kind** is **Auto** to start with: it knows the big coins and indexes, and anything else is a stock or fund. Some symbols are both: on their own **SOL**, **LINK**, **TRX** and **UNI** are coins, and **DOW** and **COMP** are stocks. Pick **Stock or fund**, **Index** or **Crypto** to say which you mean.

The card checks a symbol before it saves anything. The line under **Symbol** says what it will save, like **Saves as BRK.B, a stock or fund**, and a symbol it can't take says so, like **The panel can't read that symbol**, **AAPL is in the list already.** or **All 8 are in use. Remove one first.** The panel reads the new symbol right away. If Yahoo Finance doesn't list it, the card says **Yahoo Finance doesn't know this symbol** under it: check the spelling on Yahoo Finance's site and remove it.

Each row has **▲** and **▼** to move it up or down, and **✕** to remove it; the buttons wait until the panel has made the change, so a double tap moves or removes once. Next to each symbol is its price and the day's change, in green or red, and under it its kind and whether its market is open now. When the prices are more than half an hour old, because the display isn't on screen, the card says when they were read. The list is saved on the panel, so it stays through a restart or an update.

## Where the prices come from

The panel reads every symbol at once from Yahoo Finance, the prices and the day's charts together, only while Stocks & Crypto is on screen, or every 10 minutes while the night hours' clock takes its place:

| When | How often |
| --- | --- |
| A coin is listed, or a listed market is open | About once a minute |
| Pre-market and after hours | Every 5 minutes |
| Every listed market is closed | Every 10 minutes, and right after the next open |
| A symbol Yahoo Finance doesn't know | It doesn't count: the rest set how often |

Each read is about 20 KB for eight symbols, and up to about 70 KB late in the UTC day with many coins, so about 1 to 4 MB an hour while the display is on screen and a market is open or a coin is listed.

**When Yahoo Finance is out.** After two failed reads in a row, or as soon as Yahoo Finance turns the panel away, the panel reads coins from Kraken and US stocks and funds from Nasdaq, along with the Nasdaq Composite and the Nasdaq-100. It tries Yahoo Finance again after 10 minutes, or 30 when Yahoo Finance turned it away (longer if Yahoo Finance asked for a longer wait), and longer each time it still fails, up to an hour, and goes back to it as soon as it answers. The card says when the panel is on Kraken and Nasdaq, or, when nothing you listed is on them, that it's waiting to try Yahoo Finance again. Kraken has BTC, ETH, SOL, DOGE, XRP, ADA, LTC, DOT, LINK, AVAX, SHIB, XLM, BCH, ATOM, TRX, UNI, SUI, PEPE, POL and TON. Other indexes, other coins and stocks on other exchanges keep their last price until Yahoo Finance is back, and go gray once it's half an hour old while their market is open. The chart keeps the day so far and adds the new prices to it.

Before the first price comes in, the ticker says **Reading prices**. If the reads keep failing before any price comes in, it says **Can't reach Yahoo Finance**, or **Can't reach Yahoo Finance, Kraken or Nasdaq** once the panel has tried those too. Check the panel's internet connection.

## Night hours and the idle display

A listed stock or index market that's open counts as something on, the way a game you follow does:

- In the [night hours](/using/brightness-and-clock/#night-hours) the main board shows the clock instead of Stocks & Crypto, unless a market you listed is open. The second area does the same. Behind the clock the panel still checks the prices every 10 minutes, so Stocks & Crypto comes back within about 10 minutes of an open.
- With the **Idle display** on **Off or dim**, the panel stays dark or dim while Stocks & Crypto is on the main board and every market you listed is closed, and lights up for the open. A list of coins alone never lights it, so pick another idle display to watch coins.
