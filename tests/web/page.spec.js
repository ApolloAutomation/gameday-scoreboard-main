// Device page (firmware/web/bundle.js) against mock_device.py. Each test
// resets the mock, optionally with state overrides, and checks what the page
// shows and what it posts back to /gameday/set.
const base = require("@playwright/test");
const { expect } = base;

// Answer every off-device request locally so the suite needs no network.
// ESPN's scoreboard feeds the picker's "On now" tab and has no games unless a test routes one.
// Uncaught page errors fail the test.
const test = base.test.extend({
  page: async ({ page, baseURL }, use) => {
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await page.route(
      (url) => url.origin !== new URL(baseURL).origin,
      (route) => route.fulfill({ status: 404, body: "" }),
    );
    await page.route("https://site.api.espn.com/**", (route) => route.fulfill({ json: { events: [] } }));
    await use(page);
    expect(errors, "uncaught errors on the page").toEqual([]);
  },
});

// wifi: the saved network list the mock answers with; undefined keeps the
// mock's default, null is firmware without the list.
const reset = (request, state, fail, wifi) =>
  request.post("/_reset", { data: wifi === undefined ? { state, fail } : { state, fail, wifi } });
const sets = async (request) =>
  (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/set").map((p) => p.q);
const expectSet = (request, q) => expect.poll(() => sets(request)).toContainEqual(q);
const deviceState = async (request) => (await request.get("/gameday/state")).json();
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// A busy panel answers late. Each /gameday/state read is taken when it
// arrives and handed to the page `ms` later, so a read that left before a tap
// comes back with the settings from before it.
const slowReads = (page, ms) =>
  page.route("**/gameday/state*", async (route) => {
    const res = await route.fetch();
    await sleep(ms);
    await route.fulfill({ response: res });
  });
// Holds the first /gameday/set for `ms` before it reaches the mock, so a later
// post sent alongside it would land first.
const slowFirstPost = (page, ms) => {
  let first = true;
  return page.route("**/gameday/set*", async (route) => {
    if (first) { first = false; await sleep(ms); }
    await route.continue();
  });
};
// Checks the texts every 100 ms for `ms`, so a late read that redraws the old
// value fails the test instead of slipping between two checks.
const staysAs = async (locator, texts, ms) => {
  for (const end = Date.now() + ms; Date.now() < end; await sleep(100))
    expect(await locator.allTextContents()).toEqual(texts);
};

const LIVE = {
  s: "IN", ts: 14, os: 10, p: 1, tt: 2, ot: 3, rz: true,
  c: "8:12 - 2nd", d: "3rd & 4 at PHI 18", lp: "Prescott pass to Lamb for 12 yards",
};

test("shows a live game and the settings from /gameday/state", async ({ page, request }) => {
  await reset(request);
  const g = (await (await request.get("/gameday/state")).json()).game;
  await reset(request, { game: { ...g, ...LIVE }, odds: false, status: "DAL 14 PHI 10 | 8:12 2nd" });
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(page.locator("#tA .abbr")).toHaveText("DAL");
  await expect(page.locator("#tA .abbr")).toHaveClass(/poss/);
  await expect(page.locator("#tB .abbr")).toHaveText("PHI");
  await expect(page.locator("#sA")).toHaveText("14");
  await expect(page.locator("#sB")).toHaveText("10");
  await expect(page.locator("#clock")).toHaveText("8:12 - 2nd");
  await expect(page.locator("#down")).toHaveText("3rd & 4 at PHI 18");
  await expect(page.locator("#down")).toHaveClass(/rz/);
  await expect(page.locator("#tA .pips i.on")).toHaveCount(2);
  await expect(page.locator("#play")).toHaveText("Last play: Prescott pass to Lamb for 12 yards");
  await expect(page.locator("#ticker")).toHaveText("DAL 14 PHI 10 | 8:12 2nd");

  await expect(page.locator("#modes button.on")).toHaveText("My team");
  await expect(page.locator("#teambar")).toBeVisible();
  await expect(page.locator("#curName")).toHaveText("Dallas Cowboys");
  await expect(page.locator('.sw[data-set="down"]')).toHaveClass(/\bon\b/);
  await expect(page.locator('.sw[data-set="odds"]')).not.toHaveClass(/\bon\b/);
  // One situation switch per sport, shown for the sports the panel follows:
  // this one follows only football.
  await expect(page.getByRole("switch", { name: "Down and distance", exact: true })).toHaveAttribute("data-set", "down");
  await expect(page.getByRole("switch", { name: "Count and at-bat" })).toBeHidden();
  await expect(page.getByRole("switch", { name: "Match stats" })).toBeHidden();
  await expect(page.locator("#favCtls .ctl.fav label")).toHaveText([
    /^1\. Dallas Cowboys/, /^2\. Houston Texans/, /^3\. Ole Miss Rebels/,
  ]);
  await expect(page.locator('.sw[data-set="today"]')).not.toHaveClass(/\bon\b/);
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "2");
  await expect(page.locator("#tzline")).toContainText("US Central");
  await expect(page.locator("#hostname")).toHaveText("gameday-2f6a70.local");
  await expect(page.locator("#ver")).toHaveText("v1.5.4");
  // The game on the board is left out of Up next.
  await expect(page.locator("#upnextList .un .n")).toHaveText(["@ Commanders", "vs Texans"]);
});

test("an upcoming game shows kickoff and network", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#clock")).toHaveText("Sun 3:25 PM");
  await expect(page.locator("#down")).toHaveText("on FOX");
  await expect(page.locator("#msg")).toBeHidden();
});

// ESPN lists a game with no kickoff time yet at midnight Eastern (Friday 11 PM
// in the page's US Central) and the panel marks it tbd: its day and "TBD".
test("a game with no kickoff time yet shows its day and TBD", async ({ page, request }) => {
  const sat = 1792814400; // 2026-10-24T04:00Z
  await reset(request);
  const g = (await deviceState(request)).game;
  await reset(request, {
    game: { ...g, id: "1", oa: "MINN", kick: sat, tbd: true, k: "Oct 24 TBD", detail: "TBD" },
    next: [
      { id: "1", kick: sat, tbd: true, oi: 135, oa: "MINN", on: "Minnesota", home: false, neutral: false, tv: "" },
      { id: "2", kick: sat + 7 * 86400, tbd: true, oi: 356, oa: "ILL", on: "Illinois", home: true, neutral: false, tv: "" },
      { id: "3", kick: 1795798800, oi: 2509, oa: "PUR", on: "Purdue", home: true, neutral: false, tv: "BTN" },
    ],
  });
  await page.goto("/");
  await expect(page.locator("#clock")).toHaveText("Oct 24 TBD");
  await expect(page.locator("#upnextList .un .w")).toHaveText(["Sat, Oct 31 TBD", /^Fri, Nov 27 11:00\sAM · BTN$/]);

  const t = (abbr, name, id) => ({ l: "ncaa", id, abbr, name });
  await reset(request, {
    mode: 4,
    next: [{ slot: 1, t: t("IOWA", "Iowa Hawkeyes", 2294), id: "1", s: "PRE", kick: sat, tbd: true, oi: 135, oa: "MINN", ts: 0, os: 0, tv: "", detail: "TBD" }],
  });
  await page.reload();
  await expect(page.locator("#upnextList .un.fav .w")).toHaveText("Sat, Oct 24 TBD");
});

test("a favorite says no game scheduled only once ESPN said so", async ({ page, request }) => {
  const t = (abbr, name, id) => ({ l: "nfl", id, abbr, name });
  await reset(request, {
    mode: 4,
    game: { s: "NOT_FOUND", l: "nfl", why: "Can't reach ESPN" },
    next: [
      { slot: 1, t: t("DAL", "Dallas Cowboys", 6) },
      { slot: 2, t: t("HOU", "Houston Texans", 34), none: true },
    ],
  });
  await page.goto("/");
  await expect(page.locator("#msg")).toHaveText("Can't reach ESPN");
  await expect(page.locator("#upnextList .un.fav .w")).toHaveText(["Not checked yet", "No game scheduled"]);
});

test("a bowl or playoff game shows its name, other games none", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#gname")).toBeHidden();

  const g = (await (await request.get("/gameday/state")).json()).game;
  await reset(request, { game: { ...g, gn: "Super Bowl LX" } });
  await page.reload();
  await expect(page.locator("#gname")).toBeVisible();
  await expect(page.locator("#gname")).toHaveText("Super Bowl LX");

  await reset(request, { game: { ...g, ...LIVE, gn: "Rose Bowl" } });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(page.locator("#gname")).toHaveText("Rose Bowl");

  await reset(request, { game: { s: "NOT_FOUND", l: "nfl", gn: "Rose Bowl" }, next: [] });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#gname")).toBeHidden();
});

test("no game renders the empty board for my team and for live modes", async ({ page, request }) => {
  await reset(request, { game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.goto("/");
  await expect(page.locator("#board")).toHaveClass(/empty/);
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#msg")).toHaveText("No upcoming game for this team");
  await expect(page.locator("#upnext")).toBeHidden();

  await reset(request, { mode: 2, game: { s: "NOT_FOUND", l: "ncaa" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No live games right now");
  await expect(page.locator("#livebar")).toBeVisible();

  await reset(request, { mode: 4, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("None of your favorites has a game coming up");
  await expect(page.locator("#favbar")).toBeVisible();

  // No team picked: My team runs the live board, so it says the same
  await reset(request, { mode: 0, team: {}, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No live games right now");
  await expect(page.locator("#curName")).toHaveText("No team chosen");
});

test("Favorite teams with no favorites follows the team, and the page says so", async ({ page, request }) => {
  const none = [{}, {}, {}, {}];
  await reset(request, { mode: 4, favs: none, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.goto("/");
  await expect(page.locator("#msg")).toHaveText("No upcoming game for this team");
  await expect(page.locator("#favName")).toHaveText("No favorites set yet");

  // The team's next games, as in My team.
  await reset(request, { mode: 4, favs: none });
  await page.reload();
  await expect(page.locator("#upnextList .un .n")).toHaveText(["@ Commanders", "vs Texans"]);

  // No team either: the live board.
  await reset(request, { mode: 4, favs: none, team: {}, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No live games right now");
});

test("an empty board shows the panel's reason, and the default only when the panel has none", async ({ page, request }) => {
  await reset(request, { game: { s: "NOT_FOUND", l: "nfl", why: "Can't reach ESPN" }, next: [] });
  await page.goto("/");
  await expect(page.locator("#board")).toHaveClass(/empty/);
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#msg")).toHaveText("Can't reach ESPN");

  await reset(request, { game: { s: "NOT_FOUND", l: "nfl", why: "Getting ready" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("Getting ready");

  // Favorites with teams has its own default, but the panel's reason wins
  await reset(request, { mode: 4, game: { s: "NOT_FOUND", l: "nfl", why: "Can't reach ESPN" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("Can't reach ESPN");

  await reset(request, { game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No upcoming game for this team");
});

test("a device that never answers /gameday/state leaves the page waiting, not broken", async ({ page, request }) => {
  await reset(request, {}, ["state"]);
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#gstate")).toHaveText("WAITING");
  await expect(page.locator("#board")).toHaveClass(/empty/);
  await expect(page.locator("#curName")).toHaveText("No team chosen");
  await expect(page.locator("#modes button")).toHaveText(["My team", "Live games", "Favorite teams"]);
  // Entity controls still come from the event stream.
  await expect(page.locator("#panelCtls .ctl")).toHaveCount(4);
});

test("Diagnostics shows the Wi-Fi history, its drops and the weak-signal hint", async ({ page, request }) => {
  await reset(request, {});
  await page.goto("/");
  await expect(page.locator("#rssi")).toHaveText("-61 dBm");
  await expect(page.locator("#wifiWeak")).toBeHidden();
  // Up an hour: every drop since start is in the day.
  await expect(page.locator("#wifiDrops")).toHaveText("1 since start");
  await expect(page.locator("#wifiLast")).toHaveText("Last: signal lost, 40 min ago, back in 42 s");
  await expect(page.locator("#wifiSpark .ln")).toHaveCount(1);
  await expect(page.locator("#wifiSpark .gap")).toHaveCount(1);
  await expect(page.locator("#wifiSpark")).toHaveAttribute("aria-label", "Signal over the last 1 h: -64 to -58 dBm");
  await expect(page.locator("#wifiFrom")).toHaveText("1 h ago");

  // A full day, weak for the last stretch, three drops in it of two kinds.
  const hist = Array(288).fill(-66);
  for (let i = 270; i < 288; i++) hist[i] = -80;
  await reset(request, { wifi: {
    rssi: -81, step: 300, hist, weak: true, drops: 5, day: 3, causes: { signal: 2, router: 1, roam: 4 },
    last: "router", last_ago: 7300,
  } });
  await page.reload();
  await expect(page.locator("#wifiWeak")).toBeVisible();
  await expect(page.locator("#wifiWeak")).toHaveText(/Move the panel closer to the router/);
  await expect(page.locator("#wifiDrops")).toHaveText("3 in 24 h");
  await expect(page.locator("#wifiLast")).toHaveText(
    "Last: dropped by the router, 2 h 2 min ago. 2 signal lost, 1 dropped by the router");
  await expect(page.locator("#wifiSpark")).toHaveAttribute("aria-label", /weak for 1 h 30 min$/);
  await expect(page.locator("#wifiFrom")).toHaveText("24 h ago");

  // No drops, and only a few minutes of history: no line yet.
  await reset(request, { wifi: { rssi: -55, step: 300, hist: [-55], weak: false, drops: 0, day: 0, causes: {} } });
  await page.reload();
  await expect(page.locator("#wifiDrops")).toHaveText("None");
  await expect(page.locator("#wifiLast")).toHaveText("");
  await expect(page.locator("#wifiSparkBox")).toBeHidden();

  // Firmware before the history: the rows stay hidden, Signal still comes from the RSSI sensor.
  await reset(request, { wifi: null });
  await page.reload();
  await expect(page.locator("#hostname")).toHaveText("gameday-2f6a70.local");
  await expect(page.locator("#wifiDiag")).toBeHidden();
});

test("a failed settings post is retried once, then reported", async ({ page, request }) => {
  await reset(request, {}, ["set"]);
  await page.goto("/");
  await expect(page.locator('.sw[data-set="opp"]')).toHaveClass(/\bon\b/);
  await page.locator('.sw[data-set="opp"]').click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  expect(await sets(request)).toEqual([{ opp: "0" }, { opp: "0" }]);
  // The switch shows what the panel still has.
  await expect(page.locator('.sw[data-set="opp"]')).toHaveClass(/\bon\b/);
});

test("mode pills post the mode and swap the settings bar", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#modes button.on")).toHaveText("My team");
  await page.locator('#modes button[data-mode="5"]').click();
  await expectSet(request, { mode: "5" });
  await expect(page.locator("#modes button.on")).toHaveText("Live games");
  await expect(page.locator("#livebar")).toBeVisible();
  await expect(page.locator("#teambar")).toBeHidden();

  await page.locator("#rotate").fill("45");
  await page.locator("#rotate").press("Enter");
  await expectSet(request, { rotate: "30" }); // the page clamps to 1..30
  await expect(page.locator("#rotate")).toHaveValue("30");

  await page.locator('#modes button[data-mode="0"]').click();
  await expectSet(request, { mode: "0" });
  await expect(page.locator("#teambar")).toBeVisible();
  await expect(page.locator("#livebar")).toBeHidden();
});

test("favorite teams mode shows each favorite and posts its settings", async ({ page, request }) => {
  const t = (abbr, name, id) => ({ l: "nfl", id, abbr, name });
  await reset(request, {
    mode: 4,
    shown: 2,
    locked: true,
    next: [
      { slot: 1, t: t("DAL", "Dallas Cowboys", 6), id: "1", s: "PRE", kick: 1792531500, oi: 21, oa: "PHI", ts: 0, os: 0, tv: "FOX", detail: "" },
      { slot: 2, t: t("HOU", "Houston Texans", 34), id: "5", s: "IN", kick: 1792520000, oi: 30, oa: "JAX", ts: 14, os: 10, tv: "CBS", detail: "8:12 - 2nd" },
    ],
  });
  await page.goto("/");
  await expect(page.locator("#favbar")).toBeVisible();
  await expect(page.locator("#favName")).toHaveText("Locked on Houston Texans");
  await expect(page.locator("#upnextList .un.fav .n")).toHaveText(["DAL vs PHI", "HOU vs JAX"]);
  await expect(page.locator("#upnextList .un.shown .w")).toHaveText("14 - 10 · 8:12 - 2nd");

  await page.locator("#lockon").fill("200");
  await page.locator("#lockon").press("Enter");
  await expectSet(request, { lockon: "120" });
  await page.locator("#release").selectOption("300");
  await expectSet(request, { release: "300" });
  await expect(page.locator("#favrotate")).toBeHidden();
  await page.locator("#collide").selectOption("1");
  await expectSet(request, { collide: "1" });
  await expect(page.locator("#favrotate")).toBeVisible();
  await page.locator("#favrotate").fill("5");
  await page.locator("#favrotate").press("Enter");
  await expectSet(request, { rotate: "5" });
});

test("the team picker searches the team list and posts the team", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#curName")).toHaveText("Dallas Cowboys");
  await page.locator("#pick").click();
  await expect(page.locator("#modal")).toHaveClass(/open/);
  await expect(page.locator(".tabs button.on")).toHaveText("NFL");
  await page.locator("#q").fill("eagles");
  await expect(page.locator("#tiles .tile .n")).toContainText(["Philadelphia Eagles", "Boston College Eagles"]);
  await page.locator("#tiles .tile", { hasText: "Philadelphia Eagles" }).click();
  await expectSet(request, { team: "nfl:21" });
  await expect(page.locator("#modal")).not.toHaveClass(/open/);
  await expect(page.locator("#curName")).toHaveText("Philadelphia Eagles");
});

test("the picker opens on today's live games and follows a side", async ({ page, request }) => {
  const side = (homeAway, id, abbr, name, score) => ({ homeAway, score, team: { id, abbreviation: abbr, shortDisplayName: name } });
  await page.route("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard*", (route) =>
    route.fulfill({
      json: {
        events: [{
          id: "401772001",
          date: "2026-10-11T20:25Z",
          status: { type: { state: "in", shortDetail: "8:12 - 2nd" } },
          competitions: [{ competitors: [side("home", "6", "DAL", "Cowboys", "14"), side("away", "21", "PHI", "Eagles", "10")] }],
        }],
      },
    }),
  );
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#curName")).toHaveText("Dallas Cowboys");
  await page.locator("#pick").click();
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  await expect(page.locator("#tiles .gh")).toHaveText(["Live now"]);
  await expect(page.locator("#tiles .side.on .n")).toHaveText("Cowboys");
  await page.locator("#tiles .side", { hasText: "Eagles" }).click();
  await expectSet(request, { team: "nfl:21" });
  await expect(page.locator("#modal")).not.toHaveClass(/open/);
});

test("the On now tab asks ESPN for every college game, not the Top 25", async ({ page, request }) => {
  // Without groups= the college basketball scoreboard lists only ranked teams' games.
  const side = (homeAway, id, abbr, name) => ({ homeAway, score: "40", team: { id, abbreviation: abbr, shortDisplayName: name } });
  const asked = {};
  for (const [lg, path] of [["mcbb", "basketball/mens-college-basketball"], ["ncaa", "football/college-football"]]) {
    await page.route(`https://site.api.espn.com/apis/site/v2/sports/${path}/scoreboard*`, (route) => {
      asked[lg] = new URL(route.request().url()).searchParams;
      const events = lg !== "mcbb" ? [] : [{
        id: "401820001",
        date: "2026-02-14T19:00Z",
        status: { type: { state: "in", shortDetail: "12:30 - 2nd Half" } },
        competitions: [{ competitors: [side("home", "2006", "AKR", "Akron"), side("away", "2000", "ACU", "Abilene Chrstn")] }],
      }];
      return route.fulfill({ json: { events } });
    });
  }
  await reset(request);
  await page.goto("/");
  await page.locator("#pick").click();
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  await expect(page.locator("#tiles .side .n")).toHaveText(["Abilene Chrstn", "Akron"]);
  expect(asked.mcbb.get("groups")).toBe("50");
  expect(asked.mcbb.get("limit")).toBe("300");
  expect(asked.mcbb.get("dates")).toMatch(/^\d{8}$/);
  expect(asked.ncaa.get("groups")).toBe("80");
});

test("a college team's rank shows before its abbreviation on the board and in the lists", async ({ page, request }) => {
  // On now (it opens there with a game in progress): ESPN's curatedRank, 99
  // for an unranked side
  const side = (homeAway, id, abbr, name, rank) =>
    ({ homeAway, score: "0", curatedRank: { current: rank }, team: { id, abbreviation: abbr, shortDisplayName: name } });
  await page.route("https://site.api.espn.com/apis/site/v2/sports/football/college-football/scoreboard*", (route) =>
    route.fulfill({
      json: {
        events: [{
          id: "401856723",
          date: "2026-10-17T23:30Z",
          status: { type: { state: "in", shortDetail: "8:12 - 2nd" } },
          competitions: [{ competitors: [side("home", "2633", "TENN", "Tennessee", 14), side("away", "2", "AUB", "Auburn", 99)] }],
        }],
      },
    }),
  );
  const g = (await deviceState(request)).game;
  await reset(request, {
    team: { l: "ncaa", id: 194, abbr: "OSU", name: "Ohio State Buckeyes" },
    game: { ...g, l: "ncaa", ta: "OSU", ti: 194, tr: "9-1", trk: 5, oa: "MICH", oi: 130, or: "8-2", ork: 12, tc: "BA0C2F", oc: "00274C" },
    favs: [{}, {}, {}, {}],
  });
  await page.goto("/");
  await expect(page.locator("#tA .abbr")).toHaveText("#5 OSU");
  await expect(page.locator("#tA .abbr .rk")).toHaveText("#5");
  await expect(page.locator("#tA .rec")).toHaveText("9-1");
  await expect(page.locator("#tB .abbr")).toHaveText("#12 MICH");

  // On now
  await page.locator("#pick").click();
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  await expect(page.locator("#tiles .side .a")).toHaveText(["AUB", "#14 TENN"]);
  await page.keyboard.press("Escape");
  await expect(page.locator("#modal")).not.toHaveClass(/open/);

  // Unranked: the abbreviation alone
  await reset(request, { game: { ...g, ork: undefined } });
  await page.reload();
  await expect(page.locator("#tA .abbr")).toHaveText("DAL");
  await expect(page.locator("#tA .abbr .rk")).toHaveCount(0);

  // Favorite teams: each favorite's game with its ranks
  const t = (l, abbr, name, id) => ({ l, id, abbr, name });
  await reset(request, {
    mode: 4,
    shown: 1,
    favs: [t("ncaa", "OSU", "Ohio State Buckeyes", 194), t("nfl", "DAL", "Dallas Cowboys", 6), {}, {}],
    next: [
      { slot: 1, t: t("ncaa", "OSU", "Ohio State Buckeyes", 194), id: "1", s: "PRE", kick: 1792531500, oi: 130, oa: "MICH", trk: 5, ork: 12, ts: 0, os: 0, tv: "FOX", detail: "" },
      { slot: 2, t: t("nfl", "DAL", "Dallas Cowboys", 6), id: "2", s: "PRE", kick: 1792531500, oi: 21, oa: "PHI", ts: 0, os: 0, tv: "CBS", detail: "" },
    ],
  });
  await page.reload();
  await expect(page.locator("#upnextList .un.fav .n")).toHaveText(["#5 OSU vs #12 MICH", "DAL vs PHI"]);

  // My team's Up next: a ranked opponent's rank before its name
  await reset(request, {
    team: { l: "ncaa", id: 103, abbr: "BC", name: "Boston College Eagles" },
    favs: [{}, {}, {}, {}],
    game: { ...g, l: "ncaa", id: "401858214", ta: "BC", ti: 103, oa: "RUTG", oi: 164 },
    next: [
      { id: "401858214", kick: 1792531500, oi: 164, oa: "RUTG", on: "Rutgers", home: true, neutral: false, tv: "ESPN2" },
      { id: "401858242", kick: 1793136300, oi: 259, oa: "VT", on: "Virginia Tech", home: true, neutral: false, tv: "" },
      { id: "401858246", kick: 1793741100, oi: 2567, oa: "SMU", on: "SMU", ork: 17, home: false, neutral: false, tv: "ACCN" },
    ],
  });
  await page.reload();
  await expect(page.locator("#upnextList .un .n")).toHaveText(["vs Virginia Tech", "@ #17 SMU"]);
});

test("a rank that doesn't fit the board's team column on a narrow phone is left out", async ({ page, request }) => {
  await reset(request);
  const g = (await deviceState(request)).game;
  await reset(request, {
    team: { l: "ncaa", id: 26, abbr: "UCLA", name: "UCLA Bruins" },
    favs: [{}, {}, {}, {}],
    game: { ...g, l: "ncaa", ta: "UCLA", ti: 26, tr: "9-1", trk: 12, oa: "MICH", oi: 130, or: "8-2", ork: 25 },
  });
  await page.setViewportSize({ width: 320, height: 800 });
  await page.goto("/");
  await expect(page.locator("#tA .abbr")).toHaveText("UCLA", { useInnerText: true });
  await expect(page.locator("#tA .abbr .rk")).toBeHidden();
  await expect(page.locator("#tB .abbr .rk")).toBeHidden();
  // One line: no rank wrapping above the name
  const one = await page.locator("#tA .abbr").evaluate((a) => a.getClientRects().length === 1 && a.offsetHeight < 40);
  expect(one).toBe(true);
  // Turned to a wider screen, it comes back.
  await page.setViewportSize({ width: 768, height: 800 });
  await expect(page.locator("#tA .abbr")).toHaveText("#12 UCLA");
  await expect(page.locator("#tA .abbr .rk")).toBeVisible();
  await expect(page.locator("#tB .abbr")).toHaveText("#25 MICH");
});

test("standing next to pro teams: the record by default, posts seed and place, hidden where it doesn't apply", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const sel = page.getByLabel("Standing next to pro teams");
  await expect(page.locator("#standingRow")).toBeVisible();
  // Named like the other settings, the help as its description
  await expect(page.getByRole("combobox", { name: "Standing next to pro teams", exact: true })).toBeVisible();
  await expect(sel).toHaveAccessibleDescription(/its playoff seed, or its place/);
  await expect(sel).toHaveValue("0");
  await sel.selectOption("1");
  await expectSet(request, { standing: "1" });
  await expect(sel).toHaveValue("1");
  await sel.selectOption({ label: "Place" });
  await expectSet(request, { standing: "2" });
  expect((await deviceState(request)).standing).toBe(2);

  // What the panel puts in the record's place shows under the team.
  const g = (await deviceState(request)).game;
  await reset(request, { standing: 2, game: { ...g, tr: "1st NFCE" } });
  await page.reload();
  await expect(sel).toHaveValue("2");
  await expect(page.locator("#tA .rec")).toHaveText("1st NFCE");
  await expect(page.locator("#tB .rec")).toHaveText("0-1");

  // A panel that follows only college teams: they have a rank instead.
  await reset(request, { team: { l: "ncaa", id: 194, abbr: "OSU", name: "Ohio State Buckeyes" }, favs: [{}, {}, {}, {}] });
  await page.reload();
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#standingRow")).toBeHidden();
  // Minor league teams have no seed or place from MLB: only their record.
  await reset(request, { team: { l: "milb", id: 234, abbr: "DUR", name: "Durham Bulls" }, favs: [{}, {}, {}, {}] });
  await page.reload();
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#standingRow")).toBeHidden();
  // With a pro team as a favorite, back, and saying what a minor league team shows.
  await reset(request, { team: { l: "milb", id: 234, abbr: "DUR", name: "Durham Bulls" },
    favs: [{ l: "nfl", id: 6, abbr: "DAL", name: "Dallas Cowboys" }, {}, {}, {}] });
  await page.reload();
  await expect(page.locator("#standingRow")).toBeVisible();
  await expect(sel).toHaveAccessibleDescription(/minor league teams always their record/);
  // Nothing followed yet: shown.
  await reset(request, { team: {}, favs: [{}, {}, {}, {}] });
  await page.reload();
  await expect(page.locator("#standingRow")).toBeVisible();
  // Firmware from before the setting sends none.
  await reset(request, { standing: null });
  await page.reload();
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#standingRow")).toBeHidden();
});

const PGA = { l: "pga", id: 1106, abbr: "PGA", name: "PGA Tour" };

test("a golf tour is a team on the Golf tab, and its leaderboard shows on the board", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await page.locator("#pick").click();
  await page.locator(".tabs button", { hasText: "Golf" }).click();
  await expect(page.locator(".tabs button.on")).toHaveText("Golf");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["PGA Tour", "LPGA Tour"]);
  await expect(page.locator("#tiles .tile .a")).toHaveText(["Golf · PGA", "Golf · LPGA"]);
  await page.locator("#tiles .tile", { hasText: "PGA Tour" }).first().click();
  await expectSet(request, { team: "pga:1106" });

  await reset(request, {
    team: PGA, next: [], status: "World Wide Technology Championship | Round 2 | 1 James -15",
    game: {
      s: "IN", l: "pga", sport: 5, gn: "World Wide Technology Championship", id: "401850979",
      golf: { st: "Round 2", ph: 1, round: 2, rows: [
        { p: "1", n: "James", s: "-15", t: "F" }, { p: "2", n: "Malnati", s: "-14", t: "12" },
        { p: "T4", n: "McCarthy", s: "E", t: "F" }, { p: "T4", n: "Jaeger", s: "+2", t: "8:27p" }] },
    },
  });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(page.locator("#gname")).toHaveText("World Wide Technology Championship");
  await expect(page.locator("#golf")).toBeVisible();
  await expect(page.locator("#golfSt")).toHaveText("Round 2");
  await expect(page.locator("#golfRows li")).toHaveCount(4);
  await expect(page.locator("#golfRows .n")).toHaveText(["James", "Malnati", "McCarthy", "Jaeger"]);
  await expect(page.locator("#golfRows .p")).toHaveText(["1", "2", "T4", "T4"]);
  await expect(page.locator("#golfRows .s.u")).toHaveText(["-15", "-14"]);
  await expect(page.locator("#golfRows .s.o")).toHaveText(["+2"]);
  await expect(page.locator("#golfRows .t")).toHaveText(["F", "12", "F", "8:27p"]);
  await expect(page.locator("#board .row")).toBeHidden();
  await expect(page.locator("#curName")).toHaveText("PGA Tour");
  await expect(page.locator("#curLg")).toHaveText("Golf");
  await expect(page.locator("#upnext")).toBeHidden();

  // The picker opens on the Golf tab for a tour, LPGA's included
  await reset(request, { team: { l: "lpga", id: 1107, abbr: "LPGA", name: "LPGA Tour" }, next: [] });
  await page.reload();
  await page.locator("#pick").click();
  await expect(page.locator(".tabs button.on")).toHaveText("Golf");
  await expect(page.locator("#tiles .tile.on .n")).toHaveText("LPGA Tour");
  await page.getByRole("button", { name: "Close" }).click();

  // Nothing listed for the tour
  await reset(request, { team: PGA, next: [], game: { s: "NOT_FOUND", l: "pga" } });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No upcoming event for this tour");
  await expect(page.locator("#golf")).toBeHidden();
});

test("a golf event before it starts shows its dates, and a final the next event", async ({ page, request }) => {
  await reset(request, {
    team: PGA, next: [], tz_iana: "America/Chicago",
    game: { s: "PRE", l: "pga", sport: 5, gn: "Charles Schwab Cup Championship", id: "401832050",
            golf: { st: "Upcoming", ph: 0, start: 1794459600, end: 1794718800, rows: [] } },
  });
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#golfWhen")).toHaveText("Nov 12-15");
  await expect(page.locator("#golfRows li")).toHaveCount(0);

  await reset(request, {
    team: PGA, next: [], tz_iana: "America/Chicago",
    game: { s: "POST", l: "pga", sport: 5, gn: "Korn Ferry Tour Championship", id: "401837451", done: true,
            golf: { st: "Final", ph: 3, rows: [{ p: "1", n: "Hitt", s: "-20", t: "", w: true }, { p: "2", n: "Brown", s: "-18", t: "" }],
                    next: { name: "The RSM Classic", start: 1795064400, end: 1795323600 } } },
  });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("FINAL");
  await expect(page.locator("#golfWhen")).toHaveText("Next: The RSM Classic, Nov 19-22");
  await expect(page.locator("#golfRows li.w .n")).toHaveText("Hitt");
});

const F1 = { l: "f1", id: 2030, abbr: "F1", name: "Formula 1" };

test("a racing series is a team on the Racing tab, and its session's order shows on the board", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await page.locator("#pick").click();
  await page.locator(".tabs button", { hasText: "Racing" }).click();
  await expect(page.locator(".tabs button.on")).toHaveText("Racing");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["Formula 1", "NASCAR Cup Series"]);
  await expect(page.locator("#tiles .tile .a")).toHaveText(["Racing · F1", "Racing · NASCAR"]);
  // ESPN has no NASCAR logo: its tile shows none rather than a broken image
  await expect(page.locator("#tiles .tile", { hasText: "NASCAR" }).locator("img")).toBeHidden();
  await expect(page.locator("#tiles .tile", { hasText: "Formula 1" }).locator("img")).toHaveAttribute("src", /leagues\/500-dark\/f1\.png$/);
  await page.locator("#tiles .tile", { hasText: "Formula 1" }).first().click();
  await expectSet(request, { team: "f1:2030" });

  // Between sessions: the last one's order, car numbers and gaps, and the next session
  await reset(request, {
    team: F1, next: [], tz_iana: "America/Chicago",
    status: "MSC Cruises Sao Paulo Grand Prix | Practice 2 final | 1 Antonelli 1:10.763",
    game: {
      s: "IN", l: "f1", sport: 6, gn: "MSC Cruises Sao Paulo Grand Prix", id: "600057448",
      race: { st: "Practice 2 final", ph: 2, start: 1793979000, end: 1794157200, loc: "Autodromo Jose Carlos Pace",
              nx: "Practice 3", at: 1794065400, rows: [
                { p: "1", n: "Antonelli", v: "12", d: "1:10.763" }, { p: "2", n: "Norris", v: "1", d: "+0.253" },
                { p: "3", n: "Verstappen", v: "3", d: "+0.259" }] },
    },
  });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("PRACTICE 2 FINAL");
  await expect(page.locator("#gname")).toHaveText("MSC Cruises Sao Paulo Grand Prix");
  await expect(page.locator("#golf")).toBeVisible();
  await expect(page.locator("#golf")).toHaveClass(/race/);
  await expect(page.locator("#golfSt")).toHaveText("Practice 2 final");
  await expect(page.locator("#golfWhen")).toHaveText("Next: Practice 3, Sat 9:30 AM");
  await expect(page.locator("#golfRows .p")).toHaveText(["1", "2", "3"]);
  await expect(page.locator("#golfRows .n")).toHaveText(["Antonelli", "Norris", "Verstappen"]);
  await expect(page.locator("#golfRows .s")).toHaveText(["12", "1", "3"]);
  await expect(page.locator("#golfRows .t")).toHaveText(["1:10.763", "+0.253", "+0.259"]);
  await expect(page.locator("#board .row")).toBeHidden();
  await expect(page.locator("#curName")).toHaveText("Formula 1");
  await expect(page.locator("#curLg")).toHaveText("Racing");

  // The race final: the winner in gold, and the next race weekend
  await reset(request, {
    team: F1, next: [], tz_iana: "America/Chicago",
    game: {
      s: "POST", l: "f1", sport: 6, gn: "MSC Cruises Sao Paulo Grand Prix", id: "600057448", done: true,
      race: { st: "Final", ph: 3, rows: [{ p: "1", n: "Antonelli", v: "12", d: "", w: true }, { p: "2", n: "Norris", v: "1", d: "+3.120" }],
              next: { name: "Heineken Las Vegas Grand Prix", start: 1795134600, end: 1795320000 } },
    },
  });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("FINAL");
  await expect(page.locator("#golfRows li.w .n")).toHaveText("Antonelli");
  await expect(page.locator("#golfWhen")).toHaveText("Next: Heineken Las Vegas Grand Prix, Nov 19-21");

  // The picker opens on the Racing tab for a series, NASCAR's included
  await reset(request, { team: { l: "nascar", id: 2021, abbr: "NASCAR", name: "NASCAR Cup Series" }, next: [] });
  await page.reload();
  await expect(page.locator("#curLogo")).toBeHidden();
  await page.locator("#pick").click();
  await expect(page.locator(".tabs button.on")).toHaveText("Racing");
  await expect(page.locator("#tiles .tile.on .n")).toHaveText("NASCAR Cup Series");
  await page.getByRole("button", { name: "Close" }).click();

  // Nothing listed for the series
  await reset(request, { team: F1, next: [], game: { s: "NOT_FOUND", l: "f1" } });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No upcoming race for this series");
  await expect(page.locator("#golf")).toBeHidden();
});

test("golf and racing stay out of favorites, Live games and On now", async ({ page, request }) => {
  let golfReads = 0;
  await page.route("https://site.api.espn.com/apis/site/v2/sports/golf/**", (route) => {
    golfReads++;
    return route.fulfill({ json: { events: [] } });
  });
  await page.route("https://site.api.espn.com/apis/site/v2/sports/racing/**", (route) => {
    golfReads++;
    return route.fulfill({ json: { events: [] } });
  });
  await reset(request);
  await page.goto("/");
  // Adding a favorite: no Golf or Racing tab, and no tour or series in the search
  await page.locator("#favAdd").click();
  await expect(page.locator("#modal")).toHaveClass(/open/);
  await expect(page.locator(".tabs button", { hasText: "Golf" })).toBeHidden();
  await expect(page.locator(".tabs button", { hasText: "Racing" })).toBeHidden();
  // "pga", not "tour": the Asheville Tourists are a team
  for (const q of ["pga", "golf", "racing", "formula", "nascar"]) {
    await page.locator("#q").fill(q);
    await expect(page.locator("#tiles .none")).toHaveText("No team matches");
  }
  await page.getByRole("button", { name: "Close" }).click();
  // Choosing the team: the Golf tab is back, and the tour is found by its
  // name or by "golf"
  await page.locator("#pick").click();
  await expect(page.locator(".tabs button", { hasText: "Golf" })).toBeVisible();
  await page.locator("#q").fill("pga tour");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["PGA Tour", "LPGA Tour"]);
  await page.locator("#q").fill("golf");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["PGA Tour", "LPGA Tour"]);
  await expect(page.locator(".tabs button", { hasText: "Racing" })).toBeVisible();
  await page.locator("#q").fill("racing");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["Formula 1", "NASCAR Cup Series"]);
  await page.locator("#q").fill("formula");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["Formula 1"]);
  await page.locator("#q").fill("nascar");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["NASCAR Cup Series"]);
  // On now never reads a golf or racing scoreboard
  await page.locator(".tabs button", { hasText: "On now" }).click();
  await expect(page.locator("#tiles .none")).toHaveText("No games today");
  expect(golfReads).toBe(0);
  await page.getByRole("button", { name: "Close" }).click();
  // Live games' chips are the twelve leagues it scans
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.reload();
  await expect(page.locator("#liveChips .chip")).toHaveText(["NFL", "NCAAF", "MLB", "MiLB", "MLS", "EPL", "NBA", "WNBA", "NCAAM", "NHL", "AHL", "ECHL"]);
  expect(golfReads).toBe(0);
});

test("ticker, celebration and startup switches post 0 and 1", async ({ page, request }) => {
  await reset(request, { play: false });
  await page.goto("/");
  await expect(page.locator('.sw[data-set="down"]')).toHaveClass(/\bon\b/);
  await page.locator('.sw[data-set="down"]').click();
  await expectSet(request, { down: "0" });
  await page.locator('.sw[data-set="play"]').click();
  await expectSet(request, { play: "1" });
  await page.locator('.sw[data-set="bootaddr"]').click();
  await expectSet(request, { bootaddr: "0" });
  await page.locator('.sw[data-set="today"]').click();
  await expectSet(request, { today: "1" });
  await expect(page.locator('.sw[data-set="down"]')).not.toHaveClass(/\bon\b/);
  await expect(page.locator('.sw[data-set="play"]')).toHaveClass(/\bon\b/);
});

test("big-play switches post the alert mask, for the sports the panel follows", async ({ page, request }) => {
  const sw = (name) => page.getByRole("switch", { name, exact: true });
  const ALL = 1048575;  // every alert type on (kAlertBits in set_request.h)
  // A Cowboys panel with no favorites: football's switches only.
  await reset(request, { favs: [{}, {}, {}, {}] });
  await page.goto("/");
  await expect(sw("Football: Touchdown")).toBeChecked();
  await expect(sw("Football: Fumble recovery")).toBeChecked();
  await expect(sw("Baseball: Walk-off")).toBeHidden();
  await expect(sw("Basketball: Lead change")).toBeHidden();
  await expect(sw("Favorite teams only")).not.toBeChecked();
  // Interception is bit 5, safety bit 4.
  await sw("Football: Interception").click();
  await expectSet(request, { alerts: String(ALL - 32) });
  await expect(sw("Football: Interception")).not.toBeChecked();
  await sw("Football: Safety").click();
  await expectSet(request, { alerts: String(ALL - 32 - 16) });
  await expect(sw("Football: Safety")).not.toBeChecked();
  await expect(sw("Football: Touchdown")).toBeChecked();
  expect((await deviceState(request)).alerts).toBe(ALL - 48);
  await sw("Football: Interception").click();
  await expectSet(request, { alerts: String(ALL - 16) });
  await sw("Favorite teams only").click();
  await expectSet(request, { alertfav: "1" });
  await expect(sw("Favorite teams only")).toBeChecked();

  // A Yankees panel with an Arsenal and a Pacers favorite.
  await reset(request, {
    team: { l: "mlb", id: 10, abbr: "NYY", name: "New York Yankees" },
    favs: [{ l: "epl", id: 359, abbr: "ARS", name: "Arsenal" }, { l: "nba", id: 11, abbr: "IND", name: "Indiana Pacers" }, {}, {}],
    alerts: ALL - (1 << 18),
  });
  await page.reload();
  await expect(sw("Football: Touchdown")).toBeHidden();
  await expect(sw("Hockey: Goal")).toBeHidden();
  await expect(sw("Baseball: Grand slam")).toBeChecked();
  await expect(sw("Soccer: Red card")).toBeChecked();
  await expect(sw("Basketball: Final")).not.toBeChecked();
  await sw("Basketball: Final").click();
  await expectSet(request, { alerts: String(ALL) });
  await expect(sw("Basketball: Final")).toBeChecked();

  // A minor league team alone is baseball too: its switches and the count,
  // with the hints saying what a minor league game gets.
  await reset(request, { team: { l: "milb", id: 234, abbr: "DUR", name: "Durham Bulls" }, favs: [{}, {}, {}, {}] });
  await page.reload();
  await expect(sw("Baseball: Runs")).toBeVisible();
  await expect(sw("Baseball: Walk-off")).toBeChecked();
  await expect(sw("Football: Touchdown")).toBeHidden();
  await expect(sw("Soccer: Goal")).toBeHidden();
  await expect(sw("Count and at-bat")).toBeVisible();
  await expect(sw("Down and distance")).toBeHidden();
  // Favorite players' switch shows whatever the sports.
  await expect(page.locator("#alertCtls .alertSport:visible .sub")).toHaveText(["Baseball", "Favorite players"]);
  await expect(page.locator("#alertCtls .alertSport:visible").first()).toContainText("every minor league run");
  // Picked by hand as the one Live games league, with no team, the same.
  await reset(request, { team: {}, favs: [{}, {}, {}, {}], live_auto: false, live: ["milb"] });
  await page.reload();
  await expect(sw("Baseball: Grand slam")).toBeVisible();
  await expect(sw("Hockey: Goal")).toBeHidden();

  // Nothing picked: every sport's.
  await reset(request, { team: {}, favs: [{}, {}, {}, {}] });
  await page.reload();
  await expect(sw("Hockey: Goal")).toBeVisible();
  await expect(sw("Soccer: Goal")).toBeVisible();
  await expect(page.locator("#alertCtls .ctl")).toHaveCount(20);
  // Each sport's heading sits as far from the switch above it as the other cards' headings do.
  const heads = page.locator("#alertCtls .alertSport .sub");
  await expect(heads).toHaveCount(6);
  // Favorite players' switch is the last bit, whatever the sports.
  await expect(sw("Favorite players: Scores")).toBeChecked();
  await sw("Favorite players: Scores").click();
  await expectSet(request, { alerts: String(ALL - (1 << 19)) });
  await expect(sw("Favorite players: Scores")).not.toBeChecked();
  for (const h of await heads.all()) await expect(h).toHaveCSS("margin-top", "14px");

  // Firmware from before alerts: no switches, no Favorite teams only.
  await reset(request, { alerts: null, alertfav: null });
  await page.reload();
  await expect(sw("Opponent scores too")).toBeVisible();
  await expect(page.locator("#alertCtls")).toBeHidden();
  await expect(page.locator("#alertFavRow")).toBeHidden();
});

test("big plays in other games: the switches and the stay post their settings", async ({ page, request }) => {
  const sw = (name) => page.getByRole("switch", { name, exact: true });
  const stay = page.getByRole("combobox", { name: "Stay on a big play" });
  await reset(request, {});
  await page.goto("/");
  await expect(sw("Switch to big plays in other games")).toBeChecked();
  await expect(sw("Switch from my team")).not.toBeChecked();
  await expect(stay).toHaveValue("60");
  await sw("Switch to big plays in other games").click();
  await expectSet(request, { watch: "0" });
  await expect(sw("Switch to big plays in other games")).not.toBeChecked();
  await sw("Switch from my team").click();
  await expectSet(request, { watchmine: "1" });
  await expect(sw("Switch from my team")).toBeChecked();
  await stay.selectOption("180");
  await expectSet(request, { watchstay: "180" });
  await expect.poll(async () => (await deviceState(request)).watchstay).toBe(180);

  // A stay the list doesn't have still shows.
  await reset(request, { watchstay: 40 });
  await page.reload();
  await expect(stay).toHaveValue("40");

  // Firmware from before the watcher: none of the three.
  await reset(request, { watch: null, watchstay: null, watchmine: null });
  await page.reload();
  await expect(sw("Opponent scores too")).toBeVisible();
  await expect(page.locator("#watchRow")).toBeHidden();
  await expect(page.locator("#watchStayRow")).toBeHidden();
  await expect(page.locator("#watchMineRow")).toBeHidden();
});

test("a screen reader gets each control's name, role and state", async ({ page, request }) => {
  await reset(request, { play: false });
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/on/);
  await expect(page.locator("#conn")).toHaveAttribute("aria-live", "polite");
  await expect(page.locator("#toast")).toHaveAttribute("role", "status");

  await expect(page.getByRole("switch", { name: "Down and distance" })).toBeChecked();
  await expect(page.getByRole("switch", { name: "Last play" })).not.toBeChecked();
  await page.getByRole("switch", { name: "Last play" }).click();
  await expectSet(request, { play: "1" });
  await expect(page.getByRole("switch", { name: "Last play" })).toBeChecked();
  // The entity controls built from the event stream
  await expect(page.getByRole("switch", { name: "Panel on" })).toBeChecked();
  await expect(page.getByRole("slider", { name: "Brightness", exact: true })).toHaveValue("128");
  await expect(page.getByRole("combobox", { name: /^Idle display/ })).toHaveValue("0");
  await expect(page.getByRole("switch", { name: "Night hours" })).not.toBeChecked();
  await expect(page.getByRole("slider", { name: "Ticker speed" })).toHaveValue("5");
  await expect(page.getByRole("combobox", { name: "Showing" })).toHaveValue("Scoreboard");

  const modes = page.getByRole("group", { name: "Mode" });
  await expect(modes.getByRole("button", { pressed: true })).toHaveText("My team");
  await expect(modes.getByRole("button", { pressed: false })).toHaveText(["Live games", "Favorite teams"]);
  await expect(page.getByRole("button", { name: /^One 128x64 panel/ })).toHaveAttribute("aria-pressed", "true");
  await expect(page.getByRole("button", { name: "One 64x64 panel" })).toHaveAttribute("aria-pressed", "false");

  // The picker's league tabs: one selected, in the Tab order, the arrow keys move between them.
  await page.getByRole("button", { name: "Change team" }).click();
  await expect(page.getByRole("searchbox", { name: "Search teams" })).toBeFocused();
  const tabs = page.getByRole("tablist", { name: "Leagues" });
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("NFL");
  await expect(tabs.locator('[tabindex="0"]')).toHaveText("NFL");
  await tabs.getByRole("tab", { name: "NFL" }).press("ArrowRight");
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("NCAAF");
  await expect(tabs.getByRole("tab", { name: "NCAAF" })).toBeFocused();
  await expect(page.getByRole("tabpanel").locator(".tile .a").first()).toContainText("NCAAF");
  await page.keyboard.press("End");
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("Racing");
  await page.keyboard.press("ArrowRight");
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("On now");
  await expect(tabs.locator('[tabindex="0"]')).toHaveText("On now");
  await page.getByRole("button", { name: "Close" }).click();

  await reset(request, { mode: 5, live: ["mlb", "nhl"], live_auto: false });
  await page.reload();
  await expect(modes.getByRole("button", { pressed: true })).toHaveText("Live games");
  const chips = page.getByRole("group", { name: "Leagues" });
  await expect(chips.getByRole("button", { pressed: true })).toHaveText(["MLB", "NHL"]);
  await chips.getByRole("button", { name: "NBA", exact: true }).click();
  await expectSet(request, { live: "mlb,nhl,nba" });
  await expect(chips.getByRole("button", { pressed: true })).toHaveText(["MLB", "NBA", "NHL"]);
});

test("bar build: no switch, a bar beta note and an email with the panel's name", async ({ page, request }) => {
  await reset(request, { autoupd: null, bar: { report: "support@gameday-scoreboard.com" } });
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#autoupdRow")).toBeHidden();
  await expect(page.locator("#barRow")).toBeVisible();
  const href = await page.locator("#barReport").getAttribute("href");
  expect(href).toMatch(/^mailto:support@gameday-scoreboard\.com\?subject=Bar%20panel%20gameday-2f6a70&body=/);
  expect(decodeURIComponent(href.split("body=")[1])).toContain("Panel: gameday-2f6a70");

  await reset(request);
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#barRow")).toBeHidden();
});

test("favorites post the whole list in order on add, move and remove", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const rows = page.locator("#favCtls .ctl.fav");
  await expect(rows).toHaveCount(3);
  await expect(rows.nth(0).locator("small")).toHaveText("NFL · remote button 1");

  await page.locator("#favAdd").click();
  await expect(page.locator("#modal")).toHaveClass(/open/);
  await page.locator(".tabs button", { hasText: "MLB" }).click();
  await page.locator("#tiles .tile", { hasText: "New York Yankees" }).click();
  await expectSet(request, { favs: "nfl:6,nfl:34,ncaa:145,mlb:10" });
  await expect(page.locator("#modal")).not.toHaveClass(/open/);
  await expect(rows).toHaveCount(4);
  await expect(rows.nth(3).locator("label")).toHaveText(/^4\. New York Yankees/);

  await rows.nth(0).getByRole("button", { name: "Move down" }).click();
  await expectSet(request, { favs: "nfl:34,nfl:6,ncaa:145,mlb:10" });
  await rows.nth(2).getByRole("button", { name: "Remove" }).click();
  await expectSet(request, { favs: "nfl:34,nfl:6,mlb:10" });
  await expect(page.locator("#favCtls .ctl.fav label")).toHaveText([
    /^1\. Houston Texans/, /^2\. Dallas Cowboys/, /^3\. New York Yankees/,
  ]);
});

test("moving and removing favorites from the keyboard keeps the focus on the list", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const rows = page.locator("#favCtls .ctl.fav");
  await expect(rows).toHaveCount(3);
  // Move down follows the team it moved, so Enter again moves it on.
  await rows.nth(0).getByRole("button", { name: "Move down" }).focus();
  await page.keyboard.press("Enter");
  await expectSet(request, { favs: "nfl:34,nfl:6,ncaa:145" });
  await expect(rows.nth(1).getByRole("button", { name: "Move down" })).toBeFocused();
  await page.keyboard.press("Enter");
  await expectSet(request, { favs: "nfl:34,ncaa:145,nfl:6" });
  await expect(rows.nth(2).getByRole("button", { name: "Move down" })).toBeFocused();
  // Remove lands on the row that took its place, or on the one above for the last row.
  await rows.nth(0).getByRole("button", { name: "Remove" }).focus();
  await page.keyboard.press("Enter");
  await expectSet(request, { favs: "ncaa:145,nfl:6" });
  await expect(rows.nth(0).getByRole("button", { name: "Remove" })).toBeFocused();
  await rows.nth(1).getByRole("button", { name: "Remove" }).focus();
  await page.keyboard.press("Enter");
  await expectSet(request, { favs: "ncaa:145" });
  await expect(rows.nth(0).getByRole("button", { name: "Remove" })).toBeFocused();
  await page.keyboard.press("Enter");
  await expectSet(request, { favs: "" });
  await expect(page.locator("#favAdd")).toBeFocused();
  await expect(page.locator("#favCtls .ctl.fav label")).toHaveCount(0);
});

test("the favorites list stops offering Add a favorite at the panel's limit", async ({ page, request }) => {
  const mlb = (n) => Array.from({ length: n }, (_, i) => ({ l: "mlb", id: i + 1 }));
  await reset(request, { favs: mlb(15) });
  await page.goto("/");
  await expect(page.locator("#favCtls .ctl.fav")).toHaveCount(15);
  await expect(page.locator("#favAdd")).toBeVisible();

  await reset(request, { favs: mlb(16) });
  await page.reload();
  await expect(page.locator("#favCtls .ctl.fav")).toHaveCount(16);
  await expect(page.locator("#favAdd")).toBeHidden();
  // Past the four remote slots a row has no remote button.
  await expect(page.locator("#favCtls .ctl.fav").nth(4).locator("small")).toHaveText("MLB");
});

test("a favorite names the remote button the panel gives it, with an empty slot before it", async ({ page, request }) => {
  // Slots 1 and 3 emptied on their own (fav1=none, fav3=none): the panel keeps
  // the Texans on button 2 and the Cowboys on button 4.
  await reset(request, { favs: [{}, { l: "nfl", id: 34 }, {}, { l: "nfl", id: 6 }] });
  await page.goto("/");
  const rows = page.locator("#favCtls .ctl.fav");
  await expect(rows.locator("label")).toHaveText([/^1\. Houston Texans/, /^2\. Dallas Cowboys/]);
  await expect(rows.locator("small")).toHaveText(["NFL · remote button 2", "NFL · remote button 4"]);

  // A move posts the whole list, which the panel saves from slot 1.
  await rows.nth(0).getByRole("button", { name: "Move down" }).click();
  await expectSet(request, { favs: "nfl:6,nfl:34" });
  await expect(rows.locator("small")).toHaveText(["NFL · remote button 1", "NFL · remote button 2"]);
});

test("Live games chips post the leagues, and move an old live mode to mode 5", async ({ page, request }) => {
  await reset(request, { mode: 5, live: ["mlb", "nhl"], live_auto: false });
  await page.goto("/");
  await expect(page.locator("#livebar")).toBeVisible();
  await expect(page.locator("#modes button.on")).toHaveText("Live games");
  await expect(page.locator("#liveChips .chip")).toHaveText(["NFL", "NCAAF", "MLB", "MiLB", "MLS", "EPL", "NBA", "WNBA", "NCAAM", "NHL", "AHL", "ECHL"]);
  await expect(page.locator("#liveChips .chip.on")).toHaveText(["MLB", "NHL"]);
  await page.locator("#liveChips .chip", { hasText: /^NBA$/ }).click();
  await expectSet(request, { live: "mlb,nhl,nba" });
  await expect(page.locator("#liveChips .chip.on")).toHaveText(["MLB", "NBA", "NHL"]);

  // The last league can't be switched off.
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.reload();
  await page.locator("#liveChips .chip", { hasText: /^MLB$/ }).click();
  await expect(page.locator("#toast")).toHaveText("Pick at least one league");
  expect(await sets(request)).toEqual([]);

  // Mode 2 (live college football, from before) reads as Live games with NCAAF on.
  await reset(request, { mode: 2 });
  await page.reload();
  await expect(page.locator("#modes button.on")).toHaveText("Live games");
  await expect(page.locator("#liveChips .chip.on")).toHaveText(["NCAAF"]);
  await page.locator("#liveChips .chip", { hasText: /^NFL$/ }).click();
  await expectSet(request, { live: "ncaa,nfl", mode: "5" });
});

const chip = (page, tab) => page.locator("#liveChips .chip", { hasText: new RegExp("^" + tab + "$") });
const chipsOn = (page) => page.locator("#liveChips .chip.on");

test("Live games chips match the panel through taps, a slow re-read, mode switches and a reload", async ({ page, request }) => {
  // The steps from the reported bug: an old Live college panel, MLB on, NCAAF off,
  // Favorite teams and back. The re-read after each tap is still out when
  // the next tap comes.
  await reset(request, { mode: 2 });
  await page.goto("/");
  await expect(chipsOn(page)).toHaveText(["NCAAF"]);
  await slowReads(page, 1000);
  await chip(page, "MLB").click();
  await expect(chipsOn(page)).toHaveText(["NCAAF", "MLB"]);
  await sleep(600);
  await chip(page, "NCAAF").click();
  await expect(chipsOn(page)).toHaveText(["MLB"]);
  await staysAs(chipsOn(page), ["MLB"], 2500);

  await page.locator('#modes button[data-mode="4"]').click();
  await expect(page.locator("#favbar")).toBeVisible();
  await sleep(600);
  await page.locator('#modes button[data-mode="5"]').click();
  await expect(page.locator("#livebar")).toBeVisible();
  await staysAs(chipsOn(page), ["MLB"], 2500);

  expect(await sets(request)).toEqual([{ live: "ncaa,mlb", mode: "5" }, { live: "mlb" }, { mode: "4" }, { mode: "5" }]);
  const st = await deviceState(request);
  expect([st.mode, st.live]).toEqual([5, ["mlb"]]);
  await page.reload();
  await expect(chipsOn(page)).toHaveText(["MLB"]);
});

test("chip taps faster than the page re-reads each save the list on screen", async ({ page, request }) => {
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  await expect(chipsOn(page)).toHaveText(["MLB"]);
  await slowReads(page, 1000);
  await chip(page, "NBA").click();
  await chip(page, "NHL").click();
  await chip(page, "MLB").click();
  await expect(chipsOn(page)).toHaveText(["NBA", "NHL"]);
  await expect.poll(() => sets(request)).toEqual([{ live: "mlb,nba" }, { live: "mlb,nba,nhl" }, { live: "nba,nhl" }]);
  await staysAs(chipsOn(page), ["NBA", "NHL"], 2500);
  expect((await deviceState(request)).live).toEqual(["nba", "nhl"]);
});

test("a league chip keeps the keyboard focus through its post and the re-reads after it", async ({ page, request }) => {
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  await expect(chipsOn(page)).toHaveText(["MLB"]);
  await chip(page, "NBA").focus();
  await page.keyboard.press("Enter");
  await expectSet(request, { live: "mlb,nba" });
  await expect(chipsOn(page)).toHaveText(["MLB", "NBA"]);
  // A Game Status event re-reads the state document, as a live game does every few seconds.
  await sleep(800);
  await expect(chip(page, "NBA")).toBeFocused();
  await page.keyboard.press("Tab");
  await expect(chip(page, "WNBA")).toBeFocused();
  await page.keyboard.press("Space");
  await expectSet(request, { live: "mlb,nba,wnba" });
  await sleep(800);
  await expect(chip(page, "WNBA")).toBeFocused();
});

test("Live games bar says when the leagues are automatic, and goes back to automatic", async ({ page, request }) => {
  const lg = page.locator("#liveLg");
  const back = page.locator("#liveAuto");
  // Nothing picked: the leagues of the team and favorites (NFL and NCAAF in the mock), drawn as a suggestion.
  await reset(request, { mode: 5 });
  await page.goto("/");
  await expect(lg).toHaveText("Moves on when it ends, or after Switch every. Leagues follow your team and favorites. Tap a league to choose by hand:");
  await expect(page.locator("#liveChips")).toHaveClass(/auto/);
  await expect(chipsOn(page)).toHaveText(["NFL", "NCAAF"]);
  await expect(back).toBeHidden();

  // The first tap posts the whole list with the tap applied, and the bar
  // turns manual before the device answers.
  await slowReads(page, 1000);
  await chip(page, "MLB").click();
  await expect(lg).toHaveText("Moves on when it ends, or after Switch every. Leagues:");
  await expect(page.locator("#liveChips")).not.toHaveClass(/auto/);
  await expect(back).toBeVisible();
  await expectSet(request, { live: "nfl,ncaa,mlb" });
  await staysAs(lg, ["Moves on when it ends, or after Switch every. Leagues:"], 1500);
  expect((await deviceState(request)).live_auto).toBe(false);

  // Back to automatic posts an empty list and the device works the leagues out again.
  await back.click();
  await expectSet(request, { live: "" });
  await expect(lg).toContainText("Leagues follow your team and favorites.");
  await expect(back).toBeHidden();
  await staysAs(lg, ["Moves on when it ends, or after Switch every. Leagues follow your team and favorites. Tap a league to choose by hand:"], 1500);
  await expect(chipsOn(page)).toHaveText(["NFL", "NCAAF"]);
  const st = await deviceState(request);
  expect([st.live_auto, st.live]).toEqual([true, ["nfl", "ncaa"]]);
  expect(await sets(request)).toEqual([{ live: "nfl,ncaa,mlb" }, { live: "" }]);

  // An old football live mode has fixed leagues: no automatic wording, no link.
  await reset(request, { mode: 2 });
  await page.reload();
  await expect(lg).toHaveText("Moves on when it ends, or after Switch every. Leagues:");
  await expect(back).toBeHidden();
});

test("Live games bar says when nothing is live and the panel shows your team or finals", async ({ page, request }) => {
  const name = page.locator("#liveName");
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  await expect(name).toHaveText("Following a random game in progress");

  await reset(request, { mode: 5, live: ["mlb"], live_auto: false, fallback: true });
  await page.reload();
  await expect(name).toHaveText("No live game right now, showing your team until one starts");
  await expect(page.locator("#liveLg")).toHaveText("Leagues:");

  await reset(request, { mode: 5, team: {}, recap: true, game: { s: "POST", l: "mlb" } });
  await page.reload();
  await expect(name).toHaveText("No live game right now, showing recent finals");
  await expect(page.locator("#liveLg")).toHaveText("Leagues follow your team and favorites. Tap a league to choose by hand:");
});

test("Follow this game shows in Live games and Favorite teams with a game up, and posts follow 1 and 0", async ({ page, request }) => {
  const bar = page.locator("#followbar");
  const sw = page.getByRole("switch", { name: "Follow this game" });
  const name = page.locator("#followName");
  // My team follows one team already
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(bar).toBeHidden();

  // Live games, on a game it picked
  const g = (await deviceState(request)).game;
  await reset(request, { mode: 5, game: { ...g, ...LIVE } });
  await page.reload();
  await expect(bar).toBeVisible();
  await expect(sw).not.toBeChecked();
  await expect(name).toHaveText("Follow this game");
  await expect(page.locator("#followLg")).toHaveText("Keep this game on the panel until it ends, instead of switching to another game.");
  await sw.click();
  await expectSet(request, { follow: "1" });
  await expect(sw).toBeChecked();
  await expect(name).toHaveText("Following this game");
  await expect(page.locator("#followLg")).toHaveText(/Turn this off to go back to switching games\.$/);
  await expect(bar).toHaveClass(/\bon\b/);
  await expect(page.locator("#liveName")).toHaveText("Staying on the game you follow");
  await expect(page.locator("#liveLg")).toHaveText("Leagues follow your team and favorites. Tap a league to choose by hand:");
  await expect.poll(async () => (await deviceState(request)).follow).toBe(true);
  await sw.click();
  await expectSet(request, { follow: "0" });
  await expect(sw).not.toBeChecked();
  await expect(name).toHaveText("Follow this game");
  await expect(page.locator("#liveName")).toHaveText("Following a random game in progress");
  await expect.poll(async () => (await deviceState(request)).follow).toBe(false);

  // An old football live mode shows as Live games, and follows the same way
  await reset(request, { mode: 2 });
  await page.reload();
  await expect(bar).toBeVisible();

  // Not over your team's card or recent finals with nothing live, nor an empty board
  await reset(request, { mode: 5, fallback: true });
  await page.reload();
  await expect(page.locator("#liveName")).toHaveText("No live game right now, showing your team until one starts");
  await expect(bar).toBeHidden();
  await reset(request, { mode: 5, team: {}, recap: true, game: { s: "POST", l: "mlb" } });
  await page.reload();
  await expect(page.locator("#liveName")).toHaveText("No live game right now, showing recent finals");
  await expect(bar).toBeHidden();
  await reset(request, { mode: 5, game: { s: "NOT_FOUND" } });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(bar).toBeHidden();

  // Favorite teams: the favorite on the board
  const t = (abbr, tname, id) => ({ l: "nfl", id, abbr, name: tname });
  await reset(request, {
    mode: 4,
    shown: 1,
    next: [{ slot: 1, t: t("DAL", "Dallas Cowboys", 6), id: "401772001", s: "PRE", kick: 1792531500, oi: 21, oa: "PHI", ts: 0, os: 0, tv: "FOX", detail: "" }],
  });
  await page.reload();
  await expect(page.locator("#favName")).toHaveText("Cycling through your favorites");
  await expect(bar).toBeVisible();
  await expect(page.locator("#followLg")).toHaveText("Keep this game on the panel until its final has been up for After the final, instead of moving to another favorite.");
  await sw.click();
  await expectSet(request, { follow: "1" });
  await expect(sw).toBeChecked();
  await expect(page.locator("#favName")).toHaveText("Following Dallas Cowboys");
  await expect(page.locator("#followLg")).toHaveText("The panel stays on this game until its final has been up for After the final. Turn this off to go back to your favorites.");

  // A favorite's last final is over already: nothing to follow
  await reset(request, {
    mode: 4,
    shown: 1,
    game: { ...g, s: "POST", ts: 24, os: 17 },
    next: [{ slot: 1, t: t("DAL", "Dallas Cowboys", 6), id: "401772001", s: "POST", kick: 1792531500, oi: 21, oa: "PHI", ts: 24, os: 17, tv: "FOX", detail: "Final" }],
  });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("FINAL");
  await expect(page.locator("#favName")).toHaveText("Cycling through your favorites");
  await expect(bar).toBeHidden();
  // Nor a final in Live games, the minute before it moves on
  await reset(request, { mode: 5, game: { ...g, ...LIVE, s: "POST" } });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("FINAL");
  await expect(bar).toBeHidden();

  // Favorite teams with no favorites runs as My team
  await reset(request, { mode: 4, favs: [{}, {}, {}, {}] });
  await page.reload();
  await expect(page.locator("#favName")).toHaveText("No favorites set yet");
  await expect(bar).toBeHidden();

  // Firmware from before the switch
  await reset(request, { mode: 5, game: { ...g, ...LIVE }, follow: null });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(bar).toBeHidden();
});

test("Follow this game during another game's big play follows the game the panel was showing", async ({ page, request }) => {
  const bar = page.locator("#followbar");
  const sw = page.getByRole("switch", { name: "Follow this game" });
  await reset(request);
  const g = (await deviceState(request)).game;
  // The board is on another game's big play, even one that has just ended
  await reset(request, { mode: 5, game: { ...g, ...LIVE, s: "POST" }, visit: true, visit_follow: true });
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("FINAL");
  await expect(bar).toBeVisible();
  await expect(sw).not.toBeChecked();
  await expect(page.locator("#followLg")).toHaveText("The panel is showing a big play in another game. Turn this on to go straight back to the game it was showing and keep it on the panel until it ends.");
  await sw.click();
  await expectSet(request, { follow: "1" });
  await expect.poll(async () => (await deviceState(request)).visit).toBe(false);
  await expect(sw).toBeChecked();
  await expect(page.locator("#followLg")).toHaveText("The panel stays on this game until it ends. Turn this off to go back to switching games.");
  // Favorite teams says After the final
  await reset(request, { mode: 4, shown: 1, game: { ...g, ...LIVE }, visit: true, visit_follow: true });
  await page.reload();
  await expect(bar).toBeVisible();
  await expect(page.locator("#followLg")).toHaveText("The panel is showing a big play in another game. Turn this on to go straight back to the game it was showing and keep it on the panel until its final has been up for After the final.");
  // Not when the game the panel was showing has ended meanwhile, or Favorite
  // teams had no favorite's card up: the panel couldn't follow it
  await reset(request, { mode: 5, game: { ...g, ...LIVE }, visit: true, visit_follow: false });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(bar).toBeHidden();
  await reset(request, { mode: 4, shown: 1, game: { ...g, ...LIVE }, visit: true, visit_follow: false });
  await page.reload();
  await expect(bar).toBeHidden();
  // Not in My team, which has nothing to follow
  await reset(request, { mode: 0, visit: true, visit_follow: true });
  await page.reload();
  await expect(bar).toBeHidden();
});

test("Follow this game shows what the panel has, turns off on an open page when the panel lets go, and a follow the panel ignored goes back off", async ({ page, request }) => {
  const sw = page.getByRole("switch", { name: "Follow this game" });
  const bar = page.locator("#followbar");
  const name = page.locator("#followName");
  await reset(request);
  const g = (await deviceState(request)).game;
  // Following, as the state document says
  await reset(request, { mode: 5, game: { ...g, ...LIVE }, follow: true });
  await page.clock.install();
  await page.goto("/");
  await expect(sw).toBeChecked();
  await expect(name).toHaveText("Following this game");
  await expect(page.locator("#followLg")).toHaveText("The panel stays on this game until it ends. Turn this off to go back to switching games.");

  // The page's regular read: the game ended, and the switch stays on over the final the panel still holds
  await reset(request, { mode: 5, game: { ...g, ...LIVE, s: "POST" }, follow: true });
  await page.clock.fastForward(16000);
  await expect(page.locator("#gstate")).toHaveText("FINAL");
  await expect(bar).toBeVisible();
  await expect(sw).toBeChecked();
  // The panel let go by itself after the final: the switch goes, since a final has nothing left to follow
  await reset(request, { mode: 5, game: { ...g, ...LIVE, s: "POST" }, follow: false });
  await page.clock.fastForward(16000);
  await expect(bar).toBeHidden();

  // The panel let go on a game still on (a change made in the app): the switch turns off on the open page
  await reset(request, { mode: 5, game: { ...g, ...LIVE }, follow: true });
  await page.clock.fastForward(16000);
  await expect(sw).toBeChecked();
  await reset(request, { mode: 5, game: { ...g, ...LIVE }, follow: false });
  await page.clock.fastForward(16000);
  await expect(sw).not.toBeChecked();
  await expect(name).toHaveText("Follow this game");
  await expect(page.locator("#liveName")).toHaveText("Following a random game in progress");

  // A follow=1 the panel answered but didn't take: the switch shows the tap, then the panel's off
  await page.route("**/gameday/set*", (route) => route.fulfill({ json: { ok: true } }));
  const posted = page.waitForRequest((r) => r.url().includes("/gameday/set?follow=1"));
  await sw.click();
  expect(await sw.getAttribute("aria-checked")).toBe("true");
  await posted;
  await expect(sw).not.toBeChecked();
  await expect(page.locator("#followName")).toHaveText("Follow this game");
});

test("a slow settings post isn't overtaken by the next tap's", async ({ page, request }) => {
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  await expect(chipsOn(page)).toHaveText(["MLB"]);
  await slowFirstPost(page, 800);
  await chip(page, "NBA").click();
  await chip(page, "NHL").click();
  await expect.poll(() => sets(request)).toEqual([{ live: "mlb,nba" }, { live: "mlb,nba,nhl" }]);
  expect((await deviceState(request)).live).toEqual(["mlb", "nba", "nhl"]);
  await expect(chipsOn(page)).toHaveText(["MLB", "NBA", "NHL"]);
});

test("switching modes away and back before the page re-reads saves the last mode", async ({ page, request }) => {
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  await expect(page.locator("#livebar")).toBeVisible();
  await slowReads(page, 1000);
  // Straight through: each tap's bar shows at once, before any re-read.
  await page.locator('#modes button[data-mode="4"]').click();
  expect(await page.locator("#favbar").isVisible()).toBe(true);
  await page.locator('#modes button[data-mode="0"]').click();
  expect(await page.locator("#teambar").isVisible()).toBe(true);
  await page.locator('#modes button[data-mode="5"]').click();
  expect(await page.locator("#livebar").isVisible()).toBe(true);
  await expect.poll(() => sets(request)).toEqual([{ mode: "4" }, { mode: "0" }, { mode: "5" }]);
  await staysAs(page.locator("#modes button.on"), ["Live games"], 2500);
  await expect(chipsOn(page)).toHaveText(["MLB"]);
  expect((await deviceState(request)).mode).toBe(5);
});

test("a chip tap the panel never got goes back to what the panel has", async ({ page, request }) => {
  await reset(request, { mode: 2 }, ["set"]);
  await page.goto("/");
  await expect(chipsOn(page)).toHaveText(["NCAAF"]);
  await chip(page, "MLB").click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  await expect(chipsOn(page)).toHaveText(["NCAAF"]);
  await expect(page.locator("#modes button.on")).toHaveText("Live games");
  expect((await deviceState(request)).mode).toBe(2);
});

test("install updates automatically: off by default, posts 0 and 1, hidden when the firmware has no such setting", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const sw = page.locator('.sw[data-set="autoupd"]');
  await expect(page.locator("#autoupdRow")).toBeVisible();
  await expect(sw).not.toHaveClass(/\bon\b/);
  await sw.click();
  await expectSet(request, { autoupd: "1" });
  await expect(sw).toHaveClass(/\bon\b/);
  await sw.click();
  await expectSet(request, { autoupd: "0" });

  await reset(request, { autoupd: null });
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#autoupdRow")).toBeHidden();
});

test("follow beta: off by default, posts 0 and 1, hidden when the firmware has no beta channel", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const sw = page.locator('.sw[data-set="beta"]');
  await expect(page.locator("#betaRow")).toBeVisible();
  await expect(sw).not.toHaveClass(/\bon\b/);
  await sw.click();
  await expectSet(request, { beta: "1" });
  await expect(sw).toHaveClass(/\bon\b/);
  await sw.click();
  await expectSet(request, { beta: "0" });

  await reset(request, { beta: null });
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#betaRow")).toBeHidden();
});

test("the update switches show a tap at once and keep it through a slow re-read", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator('.sw[data-set="autoupd"]')).not.toHaveClass(/\bon\b/);
  await slowReads(page, 1000);
  await page.locator('.sw[data-set="autoupd"]').click();
  await page.locator('.sw[data-set="beta"]').click();
  await staysAs(page.locator('.sw.on[data-set="autoupd"], .sw.on[data-set="beta"]'), ["", ""], 2500);
  expect(await sets(request)).toEqual([{ autoupd: "1" }, { beta: "1" }]);
  const st = await deviceState(request);
  expect([st.autoupd, st.beta]).toEqual([true, true]);
});

test("betas come before their release: the release replaces a beta, and a beta behind the release is no update", async ({ page, request }) => {
  await reset(request, { beta: true });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  let entity = { ...fw, value: "1.5.6-beta.812", current_version: "1.5.5" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6-beta.812");
  await expect(page.locator("#fwInstall")).toBeVisible();

  // The next beta is newer than the last.
  entity = { ...fw, value: "1.5.6-beta.813", current_version: "1.5.6-beta.812" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6-beta.813");

  // Follow beta on a panel that already runs the release: that release's beta is behind it.
  entity = { ...fw, value: "1.5.6-beta.813", current_version: "1.5.6" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest beta, v1.5.6");
  await expect(page.locator("#fwInstall")).toBeHidden();
  // Check for updates says nothing about the older beta it found.
  await page.locator("#actions button", { hasText: "Check for updates" }).click();
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest beta, v1.5.6", { timeout: 8000 });
  await expect(page.locator("#toast")).not.toContainText("Update available");

  // Beta turned off on a beta build: the release it led up to replaces it.
  await reset(request, { beta: false });
  entity = { ...fw, value: "1.5.6", current_version: "1.5.6-beta.813" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6");
  await expect(page.locator("#fwInstall")).toBeVisible();
  await page.locator("#actions button", { hasText: "Check for updates" }).click();
  await expect(page.locator("#toast")).toHaveText("Update available: v1.5.6", { timeout: 8000 });

  // An older release is still behind a beta of the next one.
  entity = { ...fw, value: "1.5.5", current_version: "1.5.6-beta.813" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest release, v1.5.6-beta.813");
  await expect(page.locator("#fwSub")).toContainText("The newest release is v1.5.5. Nothing to install.");
  await expect(page.locator("#fwInstall")).toBeHidden();

  // A bar build reads the beta feed too.
  await reset(request, { autoupd: null, bar: { report: "support@example.com" } });
  entity = { ...fw, value: "1.5.6-beta.812", current_version: "1.5.6-beta.813" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest beta, v1.5.6-beta.813");
  await expect(page.locator("#fwSub")).toContainText("The newest beta is v1.5.6-beta.812. Nothing to install.");
});

test("Follow beta doesn't rename what the firmware box found until the beta feed answers", async ({ page, request }) => {
  await reset(request, { beta: false });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  let entity = { ...fw, value: "1.5.5", current_version: "1.5.6-beta.813" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest release, v1.5.6-beta.813");
  await page.locator('.sw[data-set="beta"]').click();
  await expectSet(request, { beta: "1" });
  await staysAs(page.locator("#fwText"), ["You're ahead of the latest release, v1.5.6-beta.813"], 2000);
  // The beta feed answers.
  entity = { ...fw, value: "1.5.6-beta.812", current_version: "1.5.6-beta.813" };
  await page.locator("#actions button", { hasText: "Check for updates" }).click();
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest beta, v1.5.6-beta.813", { timeout: 8000 });
  await expect(page.locator("#fwSub")).toContainText("The newest beta is v1.5.6-beta.812.");
});

test("a Follow beta flip made on another page reaches the firmware box with the beta feed's answer", async ({ page, request }) => {
  await reset(request, { beta: false });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  let entity = { ...fw, value: "1.5.5", current_version: "1.5.6-beta.813" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest release, v1.5.6-beta.813");
  // Another page turns Follow beta on, and the beta feed answers before this page re-reads the settings.
  await request.post("/gameday/set?beta=1");
  await slowReads(page, 6000);
  entity = { ...fw, value: "1.5.6-beta.812", current_version: "1.5.6-beta.813" };
  await page.locator("#actions button", { hasText: "Check for updates" }).click();
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest beta, v1.5.6-beta.813", { timeout: 8000 });
  await expect(page.locator("#fwSub")).toContainText("The newest beta is v1.5.6-beta.812.");
});

test("a check right after a Follow beta flip that still finds the release feed's answer keeps the release wording", async ({ page, request }) => {
  await reset(request, { beta: false });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: { ...fw, value: "1.5.5", current_version: "1.5.6-beta.813" } }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest release, v1.5.6-beta.813");
  await page.locator('.sw[data-set="beta"]').click();
  await expectSet(request, { beta: "1" });
  // The beta feed hasn't answered yet, so the panel still offers the release feed's version.
  const checked = page.waitForResponse((r) => r.url().includes("/update/Firmware"), { timeout: 8000 });
  await page.locator("#actions button", { hasText: "Check for updates" }).click();
  await checked;
  await staysAs(page.locator("#fwText"), ["You're ahead of the latest release, v1.5.6-beta.813"], 2000);
  await expect(page.locator("#fwSub")).toContainText("The newest release is v1.5.5.");
});

test("a setting changed during Check for updates leaves the firmware box checking", async ({ page, request }) => {
  await reset(request, { autoupd: false });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: { ...fw, value: "1.5.6", current_version: "1.5.5" } }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6");
  await page.locator("#actions button", { hasText: "Check for updates" }).click();
  await expect(page.locator("#fwText")).toHaveText("Checking for updates");
  await page.locator('.sw[data-set="autoupd"]').click();
  await staysAs(page.locator("#fwText"), ["Checking for updates"], 2500);
  await expect(page.locator("#fwInstall")).toBeHidden();
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6", { timeout: 8000 });
  await expect(page.locator("#fwSub")).toContainText("It installs on its own between 3 and 5 AM.");
});

test("a setting changed while Check for updates waits for the panel's answer leaves the firmware box checking", async ({ page, request }) => {
  await reset(request, { autoupd: false });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  const entity = { ...fw, value: "1.5.6", current_version: "1.5.5" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6");
  // The panel takes 2.5 seconds to answer the check.
  await page.route("**/update/Firmware*", async (route) => {
    await sleep(2500);
    await route.fulfill({ json: entity });
  });
  await page.locator("#actions button", { hasText: "Check for updates" }).click();
  await sleep(4300);
  await page.locator('.sw[data-set="autoupd"]').click();
  await staysAs(page.locator("#fwText"), ["Checking for updates"], 1500);
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6", { timeout: 8000 });
  await expect(page.locator("#fwSub")).toContainText("It installs on its own between 3 and 5 AM.");
});

test("Check for updates gives up on a panel that never answers", async ({ page, request }) => {
  await reset(request, { autoupd: false });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  const entity = { ...fw, value: "1.5.6", current_version: "1.5.5" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6");
  await page.route("**/update/Firmware*", () => {});  // never answered
  const btn = page.locator("#actions button", { hasText: /Check/ });
  await btn.click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond", { timeout: 15000 });
  await expect(btn).toHaveText("Check for updates");
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6");
});

test("the firmware box's overnight note shows when the panel's settings land after the update", async ({ page, request }) => {
  await reset(request, { autoupd: true });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: { ...fw, value: "1.5.6", current_version: "1.5.5" } }));
  await slowReads(page, 2500);
  await page.goto("/");
  await expect(page.locator("#fwSub")).toContainText("It installs on its own between 3 and 5 AM.", { timeout: 8000 });
});

test("an update that rolled back says so and still offers Install", async ({ page, request }) => {
  await reset(request, { autoupd: true, update_failed: "2026.10.1" });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  let entity = { ...fw, value: "2026.10.1", current_version: "2026.10.0" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("The update to v2026.10.1 didn't take and the panel went back to v2026.10.0");
  await expect(page.locator("#fwSub")).toContainText("Install tries it again. It won't install this one on its own.");
  await expect(page.locator("#fwInstall")).toBeVisible();
  let asked = "";
  page.once("dialog", (d) => { asked = d.message(); d.dismiss(); });
  await page.locator("#fwInstall").click();
  expect(asked).toBe("Try v2026.10.1 again? It didn't take last time. The panel will reboot.");

  // Auto update off: nothing to say about the nightly install.
  await reset(request, { update_failed: "2026.10.1" });
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("The update to v2026.10.1 didn't take and the panel went back to v2026.10.0");
  await expect(page.locator("#fwSub")).not.toContainText("on its own");

  // The manifest moved past it: a plain update again.
  entity = { ...fw, value: "2026.10.2", current_version: "2026.10.0" };
  await reset(request, { autoupd: true, update_failed: "2026.10.1" });
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.2");
  await expect(page.locator("#fwSub")).toContainText("It installs on its own between 3 and 5 AM.");
});

test("an install that never restarts the panel stops saying Installing once the page gives up waiting", async ({ page, request }) => {
  await reset(request);
  const entity = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "",
                   release_url: "", value: "2026.10.1", current_version: "2026.10.0" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.clock.install();
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.1");
  page.once("dialog", (d) => d.accept());
  await page.locator("#fwInstall").click();
  await expect(page.locator("#fwText")).toHaveText("Installing v2026.10.1");
  await expect(page.locator("#gone")).toHaveText("Installing v2026.10.1. This page reloads when the panel is back.");
  // The download failed: the panel stays up, still offering the same version.
  await page.clock.fastForward("05:01");
  await expect(page.locator("#gone")).toBeHidden();
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.1");
  await expect(page.locator("#fwInstall")).toBeVisible();
});

test("an install the panel never got doesn't claim it's installing", async ({ page, request }) => {
  await reset(request);
  const entity = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "",
                   release_url: "", value: "2026.10.1", current_version: "2026.10.0" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.route("**/update/Firmware/install*", (route) => route.fulfill({ status: 500, body: "" }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.1");
  page.once("dialog", (d) => d.accept());
  await page.locator("#fwInstall").click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  await expect(page.locator("#gone")).toBeHidden();
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.1");
  await expect(page.locator("#fwInstall")).toBeVisible();
});

test("release notes link to the release's section of the site changelog, a beta to its commit", async ({ page, request }) => {
  await reset(request);
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "" };
  const commit = "https://github.com/ApolloAutomation/gameday-scoreboard-main/commit/abc1234";
  let entity = { ...fw, value: "2026.10.1", current_version: "2026.10.0", release_url: "https://github.com/ApolloAutomation/gameday-scoreboard-main/releases/tag/v2026.10.1" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  const link = page.locator("#fwSub a", { hasText: "Release notes" });
  await page.goto("/");
  await expect(link).toHaveAttribute("href", "https://apolloautomation.github.io/gameday-scoreboard-main/changelog/#v2026.10.1");

  // Up to date: the running release's section.
  entity = { ...fw, state: "NO UPDATE", value: "2026.10.0", current_version: "2026.10.0", release_url: "" };
  await page.reload();
  await expect(link).toHaveAttribute("href", "https://apolloautomation.github.io/gameday-scoreboard-main/changelog/#v2026.10.0");

  entity = { ...fw, value: "2026.10.1-beta.9", current_version: "2026.10.0", release_url: commit };
  await page.reload();
  await expect(link).toHaveAttribute("href", commit);
});

test("release notes link only to an https address", async ({ page, request }) => {
  await reset(request);
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "" };
  let entity = { ...fw, value: "2026.10.1-beta.9", current_version: "2026.10.0", release_url: "javascript:alert(1)" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  const link = page.locator("#fwSub a", { hasText: "Release notes" });
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.1-beta.9");
  await expect(page.locator("#fwInstall")).toBeVisible();
  await expect(link).toHaveCount(0);

  // A release still links to its changelog section.
  entity = { ...entity, value: "2026.10.1" };
  await page.reload();
  await expect(link).toHaveAttribute("href", "https://apolloautomation.github.io/gameday-scoreboard-main/changelog/#v2026.10.1");
});

test("a team picked in My team stays on the page through a slow re-read", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#curName")).toHaveText("Dallas Cowboys");
  await slowReads(page, 1000);
  await page.locator("#pick").click();
  await page.locator(".tabs button", { hasText: "NHL" }).click();
  await page.locator("#tiles .tile", { hasText: "Anaheim Ducks" }).click();
  await expect(page.locator("#curName")).toHaveText("Anaheim Ducks");
  await staysAs(page.locator("#curName"), ["Anaheim Ducks"], 2500);
  expect((await deviceState(request)).team.id).toBe(25);
});

test("favorites and switches keep each tap through a slow re-read", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const labels = page.locator("#favCtls .ctl.fav label");
  await expect(labels).toHaveCount(3);
  await slowReads(page, 1000);
  await page.locator("#favCtls .ctl.fav").nth(0).getByRole("button", { name: "Remove" }).click();
  await sleep(600);
  await page.locator("#favCtls .ctl.fav").nth(0).getByRole("button", { name: "Move down" }).click();
  await page.locator('.sw[data-set="opp"]').click();
  await sleep(600);
  await page.locator('.sw[data-set="down"]').click();
  const want = ["1. Ole Miss RebelsCollege football · remote button 1", "2. Houston TexansNFL · remote button 2"];
  await expect(labels).toHaveText(want);
  await expect(page.locator('.sw[data-set="down"]')).not.toHaveClass(/\bon\b/);
  await Promise.all([
    staysAs(labels, want, 2500),
    staysAs(page.locator('.sw.on[data-set="opp"], .sw.on[data-set="down"]'), [], 2500),
  ]);
  expect(await sets(request)).toEqual([
    { favs: "nfl:34,ncaa:145" }, { favs: "ncaa:145,nfl:34" }, { opp: "0" }, { down: "0" },
  ]);
  const st = await deviceState(request);
  expect([st.opp, st.down]).toEqual([false, false]);
});

test("situation switches: one per sport the panel follows, each posting its own key", async ({ page, request }) => {
  const sw = (name) => page.getByRole("switch", { name, exact: true });
  // Nothing picked yet: every sport's switch.
  await reset(request, { team: {}, favs: [{}, {}, {}, {}] });
  await page.goto("/");
  await expect(sw("Down and distance")).toBeVisible();
  await expect(sw("Count and at-bat")).toBeVisible();
  await expect(sw("Match stats")).toBeVisible();

  // A Yankees panel with an Arsenal favorite: baseball and soccer, no football.
  await reset(request, {
    team: { l: "mlb", id: 10, abbr: "NYY", name: "New York Yankees" },
    favs: [{ l: "epl", id: 359, abbr: "ARS", name: "Arsenal" }, {}, {}, {}],
  });
  await page.reload();
  await expect(sw("Down and distance")).toBeHidden();
  await expect(sw("Count and at-bat")).toBeVisible();
  await expect(sw("Match stats")).toBeVisible();
  await sw("Count and at-bat").click();
  await expectSet(request, { count: "0" });
  await expect(sw("Count and at-bat")).not.toBeChecked();
  await expect(sw("Match stats")).toBeChecked();
  await sw("Match stats").click();
  await expectSet(request, { match: "0" });
  const st = await deviceState(request);
  expect([st.down, st.count, st.match]).toEqual([true, false, false]);

  // Hockey only: no situation line, so no switch.
  await reset(request, { team: { l: "nhl", id: 25, abbr: "ANA", name: "Anaheim Ducks" }, favs: [{}, {}, {}, {}] });
  await page.reload();
  await expect(page.locator("#tickerCtls .ctl[data-sport]:visible")).toHaveCount(0);
  await expect(sw("Last play")).toBeVisible();

  // Live games leagues picked by hand count too.
  await reset(request, { team: { l: "nhl", id: 25, abbr: "ANA", name: "Anaheim Ducks" }, favs: [{}, {}, {}, {}],
    live_auto: false, live: ["nhl", "mls"] });
  await page.reload();
  await expect(sw("Match stats")).toBeVisible();
  await expect(sw("Down and distance")).toBeHidden();
});

test("the picker has a tab per league and follows a team from another sport", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await page.locator("#pick").click();
  // One Golf tab for both tours and one Racing tab for both series, after the leagues
  await expect(page.locator(".tabs button")).toHaveText(["On now", "NFL", "NCAAF", "MLB", "MiLB", "MLS", "EPL", "NBA", "WNBA", "NCAAM", "NHL", "AHL", "ECHL", "Golf", "Racing"]);
  await page.locator(".tabs button", { hasText: "NHL" }).click();
  await expect(page.locator(".tabs button.on")).toHaveText("NHL");
  await expect(page.locator("#tiles .tile")).toHaveCount(32);
  await page.locator("#tiles .tile", { hasText: "Anaheim Ducks" }).click();
  await expectSet(request, { team: "nhl:25" });
  await expect(page.locator("#curName")).toHaveText("Anaheim Ducks");
  await expect(page.locator("#curLg")).toHaveText("NHL");
});

test("minor league teams sit under their level in a MiLB tab, with MLB's logos", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await page.locator("#pick").click();
  await page.locator(".tabs button", { hasText: "MiLB" }).click();
  await expect(page.locator(".tabs button.on")).toHaveText("MiLB");
  await expect(page.locator("#tiles .tile")).toHaveCount(120);
  await expect(page.locator("#tiles .gh")).toHaveText(["Triple-A", "Double-A", "High-A", "Single-A"]);
  const bulls = page.locator("#tiles .tile", { hasText: "Durham Bulls" });
  await expect(bulls.locator(".a")).toHaveText("Triple-A · DUR");
  await expect(bulls.locator("img")).toHaveAttribute("src", "https://midfield.mlbstatic.com/v1/team/234/spots/64");
  // Search finds a level, across the tabs, without the headings
  await page.locator("#q").fill("single-a");
  await expect(page.locator("#tiles .tile")).toHaveCount(30);
  await expect(page.locator("#tiles .gh")).toHaveCount(0);
  await page.locator("#q").fill("bulls");
  await page.locator("#tiles .tile", { hasText: "Durham Bulls" }).click();
  await expectSet(request, { team: "milb:234" });
  await expect(page.locator("#curName")).toHaveText("Durham Bulls");
  await expect(page.locator("#curLg")).toHaveText("Minor league baseball, Triple-A");
  await expect(page.locator("#curLogo")).toHaveAttribute("src", "https://midfield.mlbstatic.com/v1/team/234/spots/64");
});

test("On now lists today's minor league games from MLB's stats service", async ({ page, request }) => {
  let asked = null;
  const side = (id, abbr, teamName, score) => ({ team: { id, abbreviation: abbr, teamName }, score });
  await page.route("https://statsapi.mlb.com/**", (route) => {
    asked = new URL(route.request().url());
    return route.fulfill({
      json: {
        dates: [{
          games: [
            { gamePk: 1, gameDate: "2026-07-04T17:05:00Z", status: { codedGameState: "F" },
              teams: { away: side(102, "RR", "Express", 9), home: side(2310, "RNO", "Aces", 8) },
              linescore: { currentInning: 11, scheduledInnings: 9 } },
            { gamePk: 2, gameDate: "2026-07-04T23:05:00Z", status: { codedGameState: "I" },
              teams: { away: side(235, "MEM", "Redbirds", 3), home: side(234, "DUR", "Bulls", 2) },
              linescore: { currentInning: 7, inningState: "Bottom", scheduledInnings: 9 } },
            { gamePk: 3, gameDate: "2026-07-04T23:35:00Z", status: { codedGameState: "D" },
              teams: { away: side(531, "SWB", "RailRiders"), home: side(552, "SYR", "Mets") } },
            { gamePk: 4, gameDate: "2026-07-05T00:05:00Z", status: { codedGameState: "S", startTimeTBD: false },
              teams: { away: side(564, "JAX", "Jumbo Shrimp"), home: side(235, "MEM", "Redbirds") } },
            // Game 2 of a doubleheader: no start time, listed five minutes after game 1
            { gamePk: 5, gameDate: "2026-07-05T00:10:00Z", status: { codedGameState: "S", startTimeTBD: true },
              teams: { away: side(564, "JAX", "Jumbo Shrimp"), home: side(235, "MEM", "Redbirds") } },
          ],
        }],
      },
    });
  });
  await reset(request);
  await page.goto("/");
  await page.locator("#pick").click();
  // A game on opens the picker on On now
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  await expect(page.locator("#tiles .gh")).toHaveText(["Live now", "Later today", "Final", "Postponed or canceled"]);
  await expect(page.locator("#tiles .game .det")).toHaveText(["Bot 7th", /^7:05\sPM$/, "TBD", "Final/11", "Postponed"]);
  await expect(page.locator("#tiles .game.live .side img").first())
    .toHaveAttribute("src", "https://midfield.mlbstatic.com/v1/team/235/spots/64");
  expect(asked.searchParams.get("sportId")).toBe("11,12,13,14");
  expect(asked.searchParams.get("fields").split(",")).toContain("startTimeTBD");
  expect(asked.searchParams.get("date")).toMatch(/^\d{4}-\d{2}-\d{2}$/);
  await page.locator("#tiles .side", { hasText: "Bulls" }).click();
  await expectSet(request, { team: "milb:234" });
  await expect(page.locator("#curName")).toHaveText("Durham Bulls");
});

test("AHL and ECHL teams have their own tabs, with HockeyTech's logos", async ({ page, request }) => {
  let feedReads = 0;
  await page.route("https://lscluster.hockeytech.com/**", (route) => {
    feedReads++;
    return route.fulfill({ status: 403, body: "" });
  });
  await reset(request);
  await page.goto("/");
  await page.locator("#pick").click();
  await page.locator(".tabs button", { hasText: /^AHL$/ }).click();
  await expect(page.locator(".tabs button.on")).toHaveText("AHL");
  await expect(page.locator("#tiles .tile")).toHaveCount(32);
  await expect(page.locator("#tiles .gh")).toHaveCount(0);
  const crunch = page.locator("#tiles .tile", { hasText: "Syracuse Crunch" });
  await expect(crunch.locator(".a")).toHaveText("AHL · SYR");
  await expect(crunch.locator("img")).toHaveAttribute("src", "https://assets.leaguestat.com/ahl/logos/50x50/324.png");
  // A logo file named for its season
  await expect(page.locator("#tiles .tile", { hasText: "Cleveland Monsters" }).locator("img"))
    .toHaveAttribute("src", "https://assets.leaguestat.com/ahl/logos/50x50/373_94.png");
  // No small copy (the panel draws its badge): the page shows the full-size logo
  await expect(page.locator("#tiles .tile", { hasText: "Hamilton Hammers" }).locator("img"))
    .toHaveAttribute("src", "https://assets.leaguestat.com/ahl/logos/457.png");
  await page.locator(".tabs button", { hasText: "ECHL" }).click();
  await expect(page.locator("#tiles .tile")).toHaveCount(30);
  await expect(page.locator("#tiles .tile", { hasText: "Greenville Swamp Rabbits" }).locator("img"))
    .toHaveAttribute("src", "https://assets.leaguestat.com/echl/logos/52.png");
  await page.locator("#tiles .tile", { hasText: "Rapid City Rush" }).click();
  await expectSet(request, { team: "echl:70" });
  await expect(page.locator("#curName")).toHaveText("Rapid City Rush");
  await expect(page.locator("#curLg")).toHaveText("ECHL");
  await expect(page.locator("#curLogo")).toHaveAttribute("src", "https://assets.leaguestat.com/echl/logos/50x50/70.png");
  // Search finds them by name across the tabs
  await page.locator("#pick").click();
  await page.locator("#q").fill("crunch");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["Syracuse Crunch"]);
  // HockeyTech's feed doesn't answer a browser on another site: On now
  // leaves the two leagues out rather than asking it
  await page.locator("#q").fill("");
  await page.locator(".tabs button", { hasText: "On now" }).click();
  await expect(page.locator("#tiles .none")).toHaveText("No games today");
  expect(feedReads).toBe(0);
});

test("the picker's league tabs and Close fit on screen, on a small phone and on a computer", async ({ page, request }) => {
  await reset(request);
  for (const width of [320, 1280]) {
    await page.setViewportSize({ width, height: 700 });
    await page.goto("/");
    await page.locator("#pick").click();
    await expect(page.locator("#modal")).toHaveClass(/open/);
    const sheet = await page.locator(".sheet").evaluate((s) => [s.scrollWidth, s.clientWidth]);
    expect(sheet[0], `the sheet scrolls sideways at ${width}px`).toBeLessThanOrEqual(sheet[1]);
    await expect(page.locator("#close")).toBeInViewport({ ratio: 1 });
    for (const tab of await page.locator(".tabs button").all()) await expect(tab).toBeInViewport({ ratio: 1 });
    await page.locator("#close").click();
    await expect(page.locator("#modal")).not.toHaveClass(/open/);
  }
});

test("the picker is a dialog that keeps the keyboard inside and hands focus back when it closes", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#curName")).toHaveText("Dallas Cowboys");
  const inDialog = () => page.evaluate(() => !!document.activeElement.closest('[role="dialog"]'));
  await page.locator("#pick").focus();
  await page.keyboard.press("Enter");
  const dialog = page.getByRole("dialog", { name: "Choose a team" });
  await expect(dialog).toBeVisible();
  await expect(dialog).toHaveAttribute("aria-modal", "true");
  await expect(page.getByRole("searchbox", { name: "Search teams" })).toBeFocused();
  // Shift+Tab from the first control wraps to the last, Tab from the last back to the first.
  await page.keyboard.press("Shift+Tab");
  expect(await inDialog()).toBe(true);
  await page.keyboard.press("Tab");
  await expect(page.getByRole("searchbox", { name: "Search teams" })).toBeFocused();
  await page.keyboard.press("Escape");
  await expect(dialog).toBeHidden();
  await expect(page.locator("#pick")).toBeFocused();

  await page.locator("#favAdd").click();
  await expect(page.getByRole("dialog", { name: "Add a favorite" })).toBeVisible();
  await page.getByRole("button", { name: "Close" }).click();
  await expect(page.locator("#favAdd")).toBeFocused();
});

test("a timeout count ESPN didn't send hides that team's pips instead of showing them used", async ({ page, request }) => {
  await reset(request);
  const g = (await (await request.get("/gameday/state")).json()).game;
  await reset(request, { game: { ...g, ...LIVE, tt: -1, ot: 0 } });
  await page.goto("/");
  await expect(page.locator("#tA .pips")).toBeHidden();
  await expect(page.locator("#tB .pips")).toBeVisible();
  await expect(page.locator("#tB .pips i.on")).toHaveCount(0);  // a real 0 still shows all used
});

test("other sports show their situation line and no timeout pips", async ({ page, request }) => {
  const mlb = {
    s: "IN", l: "mlb", sport: 1, ta: "NYY", ti: 10, ts: 3, oa: "TOR", oi: 14, os: 2, p: 0, tt: 0, ot: 0,
    c: "Bot 7th", sit: "2-1", outs: 2, bases: 5, d: "", k: "", tc: "0C2340", oc: "134A8E", id: "401800001",
  };
  await reset(request, { team: { l: "mlb", id: 10, abbr: "NYY", name: "New York Yankees" }, game: mlb, next: [] });
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(page.locator("#clock")).toHaveText("Bot 7th");
  await expect(page.locator("#down")).toHaveText("2-1, 2 outs");
  await expect(page.locator("#tA .pips")).toBeHidden();
  await expect(page.locator("#tB .pips")).toBeHidden();

  const nhl = { ...mlb, l: "nhl", sport: 3, ta: "ANA", ti: 25, oa: "LA", oi: 8, c: "12:04 - 2nd", sit: "Series tied 2-2", outs: -1 };
  await reset(request, { game: nhl, next: [] });
  await page.reload();
  await expect(page.locator("#down")).toHaveText("Series tied 2-2");
});

test("timezone follows the browser, or a zone picked by hand", async ({ page, request }) => {
  await reset(request, { tz: 0, tz_name: "US Eastern", tz_auto: true });
  await page.goto("/");
  await expectSet(request, { tz: "1" }); // the browser is pinned to America/Chicago
  await expect(page.locator("#toast")).toHaveText("Timezone set to US Central from this browser");
  await expect(page.locator("#tzline")).toContainText("US Central · set from this browser");

  await page.locator("#tzline .lnk", { hasText: "choose manually" }).click();
  await expect(page.locator("#tzpick")).toBeVisible();
  await page.locator("#tzsel").selectOption({ label: "US Pacific" });
  await expectSet(request, { tzauto: "0", tz: "4" });
  await expect(page.locator("#tzline")).toContainText("US Pacific · chosen by hand");
});

// Ojinaga keeps US Central time and Ciudad Juarez US Mountain time, both on
// US daylight saving dates, so a browser in either sets the matching US zone.
test.describe("a browser in Ojinaga", () => {
  test.use({ timezoneId: "America/Ojinaga" });
  test("sets the panel to US Central", async ({ page, request }) => {
    await reset(request, { tz: 0, tz_name: "US Eastern", tz_auto: true });
    await page.goto("/");
    await expectSet(request, { tz: "1" });
    await expect(page.locator("#toast")).toHaveText("Timezone set to US Central from this browser");
    await expect(page.locator("#tzline")).toContainText("US Central · set from this browser");
  });
});

test.describe("a browser in Ciudad Juarez", () => {
  test.use({ timezoneId: "America/Ciudad_Juarez" });
  test("sets the panel to US Mountain", async ({ page, request }) => {
    await reset(request, { tz: 0, tz_name: "US Eastern", tz_auto: true });
    await page.goto("/");
    await expectSet(request, { tz: "2" });
    await expect(page.locator("#toast")).toHaveText("Timezone set to US Mountain from this browser");
    await expect(page.locator("#tzline")).toContainText("US Mountain · set from this browser");
  });
});

test("the timezone line's choose manually and follow this browser work from the keyboard", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#tzline")).toContainText("US Central · set from this browser");
  const choose = page.locator("#tzline .lnk");
  await expect(choose).toHaveRole("button");
  await choose.focus();
  await expect(choose).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(page.locator("#tzpick")).toBeVisible();

  await reset(request, { tz: 4, tz_name: "US Pacific", tz_auto: false });
  await page.reload();
  const follow = page.getByRole("button", { name: "follow this browser (US Central)" });
  await follow.focus();
  await page.keyboard.press("Enter");
  await expectSet(request, { tzauto: "1" });
});

test("the timezone line's button keeps the keyboard focus through the page's regular re-read", async ({ page, request }) => {
  await reset(request);
  await page.clock.install();
  await page.goto("/");
  await expect(page.locator("#tzline")).toContainText("US Central · set from this browser");
  let reads = 0;
  page.on("request", (r) => { if (r.url().includes("/gameday/state")) reads++; });
  const choose = page.getByRole("button", { name: "choose manually" });
  await choose.focus();
  await page.clock.fastForward(16000);
  await expect.poll(() => reads).toBeGreaterThan(0);
  await sleep(300);
  await expect(choose).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(page.locator("#tzpick")).toBeVisible();
});

test("the panel picker asks first, then posts the new layout", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "2");
  await expect(page.locator("#panels .panelpick.on")).toContainText("One 128x64 panel");
  await expect(page.locator("#panels .panelpick")).toHaveCount(5);
  await expect(page.locator("#wiringRow")).toBeHidden();
  let asked = "";
  page.once("dialog", (d) => { asked = d.message(); d.accept(); });
  await page.locator('#panels .panelpick[data-cols="1"]').click();
  await expectSet(request, { panels: "1" });
  expect(asked).toBe("Restart the panel as 64x64?");
  await expect(page.locator("#toast")).toHaveText("Restarting as 64x64");
  await expect(page.locator('#panels .panelpick[data-cols="2"]')).toBeDisabled();
});

test("cancelling the restart keeps the layout and posts nothing", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  let asked = "";
  page.once("dialog", (d) => { asked = d.message(); d.dismiss(); });
  await page.getByRole("button", { name: "Four 64x64 panels side by side" }).click();
  expect(asked).toBe("Restart the panel as 256x64?");
  await sleep(300);
  expect(await sets(request)).toEqual([]);
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "2");
  await expect(page.getByRole("button", { name: "Four 64x64 panels side by side" })).toBeEnabled();
});

test("three wide and four wide post their counts", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  page.once("dialog", (d) => d.accept());
  await page.getByRole("button", { name: "Three 64x64 panels side by side" }).click();
  await expectSet(request, { panels: "3" });
  await expect(page.locator("#toast")).toHaveText("Restarting as 192x64");
  await reset(request, { panels: 3 });
  await page.reload();
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "3");
  page.once("dialog", (d) => d.accept());
  await page.getByRole("button", { name: "Four 64x64 panels side by side" }).click();
  await expectSet(request, { panels: "4" });
  await expect(page.locator("#toast")).toHaveText("Restarting as 256x64");
});

test("the 2x2 keeps its saved wiring, and on a 2x2 the wiring can be changed", async ({ page, request }) => {
  await reset(request, { wiring: 2 });
  await page.goto("/");
  await expect(page.locator("#wiringRow")).toBeHidden();
  page.once("dialog", (d) => d.accept());
  await page.getByRole("button", { name: "Four 64x64 panels in two rows of two" }).click();
  await expectSet(request, { panels: "2x2", wiring: "2" });
  await expect(page.locator("#toast")).toHaveText("Restarting as 128x128");

  await reset(request, { panels: 2, rows: 2, wiring: 0 });
  await page.reload();
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "2x2");
  await expect(page.locator("#wiringRow")).toBeVisible();
  await expect(page.locator("#wiringSel")).toHaveValue("0");
  // Cancelled: the choice goes back and nothing is sent.
  page.once("dialog", (d) => d.dismiss());
  await page.locator("#wiringSel").selectOption("1");
  await expect(page.locator("#wiringSel")).toHaveValue("0");
  expect(await sets(request)).toEqual([]);
  let asked = "";
  page.once("dialog", (d) => { asked = d.message(); d.accept(); });
  await page.locator("#wiringSel").selectOption("1");
  await expectSet(request, { wiring: "1" });
  expect(asked).toBe("Restart the panel with the new wiring?");
  await expect(page.locator("#toast")).toHaveText("Restarting with the new wiring");
  // Picking the two-wide picture from the 2x2 is a change of layout.
  await reset(request, { panels: 2, rows: 2, wiring: 0 });
  await page.reload();
  page.once("dialog", (d) => d.accept());
  await page.locator('#panels .panelpick[data-cols="2"]').click();
  await expectSet(request, { panels: "2" });
});

test("firmware that sends no rows or wiring shows its count and no wiring choice", async ({ page, request }) => {
  await reset(request, { panels: 1, rows: null, wiring: null });
  await page.goto("/");
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "1");
  await expect(page.locator("#wiringRow")).toBeHidden();
});

test("a layout change the panel never got doesn't claim it's restarting", async ({ page, request }) => {
  await reset(request, {}, ["set"]);
  await page.goto("/");
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "2");
  page.once("dialog", (d) => d.accept());
  await page.locator('#panels .panelpick[data-cols="1"]').click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  await expect(page.locator("#gone")).toBeHidden();
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator('#panels .panelpick[data-cols="1"]')).toBeEnabled();
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "2");
});

test("the device box shows what the panel downloaded from ESPN", async ({ page, request }) => {
  await reset(request, { espn_kb: 850, espn_reads: 1 });
  await page.goto("/");
  await expect(page.locator("#espnData")).toHaveText("850 KB in 1 read");

  await reset(request, { espn_kb: 14321, espn_reads: 1260 });
  await page.reload();
  await expect(page.locator("#espnData")).toHaveText("14.3 MB in 1260 reads");

  // Firmware from before the counter sends neither field.
  await reset(request, { espn_kb: null, espn_reads: null });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#espnData")).toHaveText("");
});

test("diagnostics say why the panel restarted and how it is doing", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#health")).toBeVisible();
  await expect(page.locator("#hStart")).toHaveText("Restart, 2 h 1 min ago");
  await expect(page.locator("#hBoots")).toHaveText("Start 2 since the power came on");
  await expect(page.locator("#hFault")).toHaveText("Crash, 1 d 1 h ago");
  await expect(page.locator("#hHeap")).toHaveText("143 KiB, largest piece 40 KiB");
  await expect(page.locator("#hEspn")).toHaveText("OK, 9 s ago");
  await expect(page.locator("#hFw")).toHaveText("1.5.4 stable, built Oct 17, 8:01 PM");
  // The Wi-Fi object, when there is one, fills in the signal.
  await expect(page.locator("#rssi")).toHaveText("-61 dBm");
  await expect(page.locator("#psram")).toHaveText("4101 KiB");

  // Pulled power: this start is the power coming on, nothing went wrong
  // since, and ESPN didn't answer the last read. No Wi-Fi object, so the
  // signal comes from the health block.
  const h = (await deviceState(request)).health;
  await reset(request, {
    health: { ...h, start: "power", boots: 1, up: 45, fault: undefined, fault_ago: undefined, rssi: undefined,
              espn: 0, espn_ago: 130, channel: "beta" },
    wifi: null,
  });
  await page.reload();
  await expect(page.locator("#hStart")).toHaveText("Power on, 45 s ago");
  await expect(page.locator("#hBoots")).toHaveText("");
  await expect(page.locator("#hFault")).toHaveText("None");
  await expect(page.locator("#hEspn")).toHaveText("No answer, 2 min ago");
  await expect(page.locator("#hFw")).toHaveText("1.5.4 beta, built Oct 17, 8:01 PM");
  await expect(page.locator("#rssi")).toHaveText("Not connected");

  // Minor league scores read from MLB: their own row, only once there is one
  await expect(page.locator("#hMlbRow")).toBeHidden();
  await reset(request, { health: { ...h, mlb: 429, mlb_ago: 40 } });
  await page.reload();
  await expect(page.locator("#hMlbRow")).toBeVisible();
  await expect(page.locator("#hMlb")).toHaveText("HTTP 429, 40 s ago");
  await expect(page.locator("#hEspn")).toHaveText("OK, 9 s ago");
  // and AHL and ECHL scores read from HockeyTech
  await expect(page.locator("#hHtRow")).toBeHidden();
  await reset(request, { health: { ...h, ht: 200, ht_ago: 12 } });
  await page.reload();
  await expect(page.locator("#hHtRow")).toBeVisible();
  await expect(page.locator("#hHt")).toHaveText("OK, 12 s ago");

  // A brownout whose time isn't known, before the first ESPN read.
  await reset(request, {
    health: { ...h, start: "brownout", fault: "brownout", fault_ago: undefined, espn: undefined, espn_ago: undefined },
  });
  await page.reload();
  await expect(page.locator("#hStart")).toHaveText("Power dip (brownout), 2 h 1 min ago");
  await expect(page.locator("#hFault")).toHaveText("Power dip (brownout)");
  await expect(page.locator("#hEspn")).toHaveText("None yet");

  // Firmware from before the health block.
  await reset(request, { health: null });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#health")).toBeHidden();

  // An empty block (the firmware's fallback when it doesn't fit) counts as none.
  await reset(request, { health: {} });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#health")).toBeHidden();
});

test("a hot or under-powered panel says why it is dimmed, and the power cap clears", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#hTemp")).toHaveText("47 C");
  await expect(page.locator("#hPower")).toHaveText("0");
  await expect(page.locator("#guard")).toBeHidden();

  const h = (await deviceState(request)).health;
  await reset(request, { health: { ...h, temp: 78.4, heat: 2 } });
  await page.reload();
  await expect(page.locator("#guard")).toBeVisible();
  await expect(page.locator("#guardName")).toHaveText("Too warm, dimming");
  await expect(page.locator("#guardText")).toContainText("at 45% of its brightness");
  await expect(page.locator("#guardClear")).toBeHidden();
  await expect(page.locator("#hTemp")).toHaveText("78 C, dimmed to 45%");

  // The power cap wins over the heat, and Clear posts power_ok.
  await reset(request, { health: { ...h, heat: 1, brownouts: 3, power: "capped" } });
  await page.reload();
  await expect(page.locator("#guardName")).toHaveText("Check the power supply");
  await expect(page.locator("#hPower")).toHaveText("3, brightness capped");
  await page.locator("#guardClear").click();
  await expect.poll(async () =>
    (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/action").map((p) => p.q.do),
  ).toContain("power_ok");
  await expect(page.locator("#guardName")).toHaveText("Too warm, dimming");
  await expect(page.locator("#guardClear")).toBeHidden();

  // Before the first temperature reading there is no temperature row.
  await reset(request, { health: { ...h, temp: undefined } });
  await page.reload();
  await expect(page.locator("#hPower")).toHaveText("0");
  await expect(page.locator("#hTempRow")).toBeHidden();

  // Firmware from before the guard sends none of its keys.
  await reset(request, { health: { ...h, temp: undefined, heat: undefined, brownouts: undefined, power: undefined } });
  await page.reload();
  await expect(page.locator("#hFault")).toHaveText("Crash, 1 d 1 h ago");
  await expect(page.locator("#hPowerRow")).toBeHidden();
  await expect(page.locator("#guard")).toBeHidden();
});

test("saved Wi-Fi networks are listed in order and one can be forgotten", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const rows = page.locator("#nets .ctl");
  await expect(page.locator("#netsCard")).toBeVisible();
  await expect(rows).toHaveCount(2);
  await expect(rows.nth(0).locator("label")).toContainText("Home");
  await expect(rows.nth(0).locator("label small")).toHaveText("Connected");
  await expect(rows.nth(1).locator("label")).toContainText("iPhone");
  await expect(page.locator("#netsEmpty")).toBeHidden();
  // Any network the panel is set up on is saved, so the hint leads with the
  // panel's own hotspot.
  await expect(page.locator("#netsCard > p.hint").first()).toHaveText(
    "The panel joins the first of these it can see, the last one that worked first."
    + " A network you set it up on, through its Game Day hotspot or the installer, is added here.");

  // Forget asks first; declining posts nothing.
  page.once("dialog", (d) => d.dismiss());
  await rows.nth(1).locator("button").click();
  await sleep(300);
  const forgets = async () =>
    (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/wifi").map((p) => p.q);
  expect(await forgets()).toEqual([]);

  page.once("dialog", (d) => d.accept());
  await rows.nth(1).locator("button").click();
  await expect.poll(forgets).toEqual([{ forget: "iPhone" }]);
  await expect(page.locator("#toast")).toHaveText("Forgot iPhone");
  await expect(rows).toHaveCount(1);
  await expect(rows.nth(0).locator("label")).toContainText("Home");

  // Forgetting the last one leaves the empty line. The panel stays on it and
  // joins it again, since ESPHome's own slot keeps the last network.
  page.once("dialog", (d) => d.accept());
  await rows.nth(0).locator("button").click();
  await expect(rows).toHaveCount(0);
  await expect(page.locator("#netsEmpty")).toBeVisible();
  await expect(page.locator("#toast")).toHaveText("Forgot Home; the panel keeps using it and saves it again when it reconnects");

  // The connected one, with another saved: it stays only until that
  // connection drops or the panel restarts.
  await reset(request);
  await page.reload();
  await expect(rows).toHaveCount(2);
  page.once("dialog", (d) => d.accept());
  await rows.nth(0).locator("button").click();
  await expect(page.locator("#toast")).toHaveText("Forgot Home; it stays connected until this connection drops or the panel restarts");

  // Firmware without the list: the section stays hidden.
  await reset(request, {}, [], null);
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#netsCard")).toBeHidden();
});

test("the stale tag follows the device's consecutive misses", async ({ page, request }) => {
  await reset(request, { misses: 2 });
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#stale")).toBeHidden();

  // Three misses is where the firmware adds "no update" to the ticker.
  await reset(request, { misses: 3, stale_s: 600 });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#stale")).toBeVisible();

  // The ticker warns with no game too, so the empty board shows it.
  await reset(request, { misses: 5, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#stale")).toBeVisible();

  await reset(request, { misses: 0, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#stale")).toBeHidden();
});

test("a Wi-Fi that wants a sign-in says how to fix it, in place of the panel's reason and the stale tag", async ({ page, request }) => {
  await reset(request, { captive: true, misses: 1, game: { s: "NOT_FOUND", l: "nfl", why: "Wi-Fi needs a sign-in" }, next: [] });
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#msg")).toHaveText("This Wi-Fi needs a sign-in. Use a home network or a hotspot.");
  await expect(page.locator("#stale")).toBeVisible();
  await expect(page.locator("#stale")).toHaveText("Wi-Fi needs a sign-in");

  // Over a game still on the board from before, only the tag says it
  await reset(request, { captive: true, misses: 1 });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#stale")).toHaveText("Wi-Fi needs a sign-in");

  // Cleared: back to the usual stale tag
  await reset(request, { captive: false, misses: 3, stale_s: 600 });
  await page.reload();
  await expect(page.locator("#stale")).toHaveText("data stale");
  await expect(page.locator("#stale")).toBeVisible();
});

test("says when the panel stops answering, and clears it when the panel is back", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#gone")).toBeHidden();

  // The panel drops off the network: the stream ends and the state read gets no answer.
  await reset(request, {}, ["down"]);
  await expect(page.locator("#gone")).toHaveText(/^Panel not answering since \d{1,2}:\d{2}\s[AP]M$/);
  await expect(page.locator("#board")).toHaveClass(/dim/);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);
  // The last score stays up, dimmed.
  await expect(page.locator("#tA .abbr")).toHaveText("DAL");

  // Back: the next poll answers and the stream reopens without waiting out its retry.
  await reset(request);
  await expect(page.locator("#gone")).toBeHidden({ timeout: 20000 });
  await expect(page.locator("#board")).not.toHaveClass(/dim/);
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
});

test("a dropped stream alone doesn't call the panel gone", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  // A phone waking up, or the stream refused: the page asks the panel, and it answers.
  await reset(request, {}, ["events"]);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);
  await staysAs(page.locator("#gone:visible"), [], 2000);
  await reset(request);
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/, { timeout: 20000 });
  await expect(page.locator("#gone")).toBeHidden();
});

test("Reboot says it's restarting and reloads when the panel is back", async ({ page, request }) => {
  await reset(request);
  page.on("dialog", (d) => d.accept());
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await page.locator("#actions button", { hasText: "Reboot" }).click();
  const restarting = "Restarting. This page reconnects when the panel is back.";
  await expect(page.locator("#gone")).toHaveText(restarting);
  await expect(page.locator("#board")).toHaveClass(/dim/);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);

  // Down for a few seconds, then back: the page reloads on its own.
  await reset(request, {}, ["down"]);
  await sleep(4500);
  await expect(page.locator("#gone")).toHaveText(restarting);
  const reloaded = page.waitForEvent("load", { timeout: 10000 });
  await reset(request);
  await reloaded;
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#gone")).toBeHidden();
});

test("both resets say the panel restarts into setup and the page won't reconnect", async ({ page, request, baseURL }) => {
  await reset(request);
  page.on("dialog", (d) => d.accept());
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await page.locator("#actions button", { hasText: "Reset Wi-Fi" }).click();
  await expect(page.locator("#gone")).toHaveText(
    "The panel is restarting into Wi-Fi setup for 15 minutes, so this page won't reconnect. Its settings are kept,"
    + " and unplugging it and plugging it back in takes it back to the Wi-Fi it already knows."
    + " To set it up again, join its \"Game Day 2f6a70\" Wi-Fi.");
  await expect(page.locator("#board")).toHaveClass(/dim/);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);

  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await page.locator("#actions button", { hasText: "Factory reset" }).click();
  await expect(page.locator("#gone")).toHaveText(
    "The panel is restarting into setup with everything erased, so this page won't reconnect."
    + " To set it up again, join its \"Game Day 2f6a70\" Wi-Fi.");
  // The mock still answers, but the page has stopped asking it.
  const asked = [];
  page.on("request", (r) => { if (r.url().startsWith(baseURL)) asked.push(r.url()); });
  await sleep(1500);
  expect(asked).toEqual([]);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);
});

test("a reset the panel never got doesn't claim it's resetting", async ({ page, request }) => {
  await reset(request);
  page.on("dialog", (d) => d.accept());
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await page.route("**/button/**", (route) => route.fulfill({ status: 500, body: "" }));
  await page.locator("#actions button", { hasText: "Factory reset" }).click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  await expect(page.locator("#gone")).toBeHidden();
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
});

test("the idle display posts its choice, shows the dim level for Off or dim, and the night hours post their times", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const idle = page.getByRole("combobox", { name: /^Idle display/ });
  await expect(page.locator("#idleCtls")).toBeVisible();
  await expect(idle).toHaveValue("0");
  await expect(page.locator("#idleHint")).toContainText("your team's next game");
  await expect(page.locator("#idleDimRow")).toBeHidden();
  await expect(page.locator("#nightRows")).toBeHidden();

  await idle.selectOption("3");
  await expectSet(request, { idle: "3" });
  await expect(page.locator("#idleHint")).toContainText("countdown");
  await idle.selectOption("4");
  await expectSet(request, { idle: "4" });
  await expect(page.locator("#idleDimRow")).toBeVisible();
  const dim = page.getByRole("slider", { name: /^Dim to/ });
  await expect(dim).toHaveValue("0");
  await dim.fill("30");
  await expectSet(request, { idledim: "30" });
  await expect(page.locator("#idledimVal")).toHaveText("30");

  const night = page.getByRole("switch", { name: "Night hours" });
  await night.click();
  await expectSet(request, { night: "1" });
  await expect(night).toBeChecked();
  await expect(page.locator("#nightRows")).toBeVisible();
  await expect(page.locator("#nightstart")).toHaveValue("22:00");
  await expect(page.locator("#nightend")).toHaveValue("07:00");
  await page.locator("#nightstart").fill("23:15");
  await expectSet(request, { nightstart: "23:15" });
  await page.locator("#nightend").fill("06:30");
  await expectSet(request, { nightend: "06:30" });
  const nightBri = page.getByRole("slider", { name: /^Night brightness/ });
  await expect(nightBri).toHaveValue("20");
  await nightBri.fill("45");
  await expectSet(request, { nightbri: "45" });

  // The panel kept every one, and a reload shows them.
  const st = await deviceState(request);
  expect([st.idle, st.idledim, st.night, st.nightstart, st.nightend, st.nightbri]).toEqual([4, 30, true, "23:15", "06:30", 45]);
  await page.reload();
  await expect(idle).toHaveValue("4");
  await expect(page.locator("#nightstart")).toHaveValue("23:15");
  await expect(page.locator("#nightend")).toHaveValue("06:30");
  await expect(page.locator("#nightbriVal")).toHaveText("45");
  await night.click();
  await expectSet(request, { night: "0" });
  await expect(page.locator("#nightRows")).toBeHidden();
});

test("the idle display keeps each tap through a slow re-read", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const idle = page.getByRole("combobox", { name: /^Idle display/ });
  await expect(idle).toHaveValue("0");
  await slowReads(page, 1000);
  await idle.selectOption("2");
  await sleep(600);
  await page.getByRole("switch", { name: "Night hours" }).click();
  await expect(idle).toHaveValue("2");
  await expect(page.locator("#nightRows")).toBeVisible();
  await Promise.all([
    staysAs(page.locator("#idleHint"),
      ["Recent final scores and the games still to come today, for your team's and your favorites' leagues."], 2500),
    staysAs(page.locator('.sw.on[data-set="night"]'), [""], 2500),
  ]);
  expect(await sets(request)).toEqual([{ idle: "2" }, { night: "1" }]);
});

test("the idle display says what the panel shows now, and hides on firmware without it", async ({ page, request }) => {
  await reset(request, { idle: 1, idle_view: "clock", night: true, night_now: true });
  await page.goto("/");
  await expect(page.locator("#idleNow")).toHaveText("Showing the clock, in the night hours, until a game you follow starts.");
  await reset(request, { idle: 4, idle_view: "clock", dark: true });
  await page.reload();
  await expect(page.locator("#idleNow")).toHaveText("The panel is dark until a game you follow starts.");
  await expect(page.locator("#idleDimRow")).toBeVisible();
  await reset(request, { idle: 2, idle_view: "" });
  await page.reload();
  await expect(page.locator("#idleCtls")).toBeVisible();
  await expect(page.locator("#idleNow")).toBeHidden();

  await reset(request, { idle: null, idledim: null, night: null, nightstart: null, nightend: null, nightbri: null });
  await page.reload();
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#idleCtls")).toBeHidden();
});

test("a night time cleared in its field isn't posted", async ({ page, request }) => {
  await reset(request, { night: true });
  await page.goto("/");
  await expect(page.locator("#nightRows")).toBeVisible();
  await page.locator("#nightstart").fill("");
  await sleep(500);
  expect(await sets(request)).toEqual([]);
  await page.locator("#nightstart").fill("21:45");
  await expectSet(request, { nightstart: "21:45" });
});

// Manual bets (components/gameday/bets.h): the Bets card, folded away at the
// end, and the bets on the board's game under the board.
const BET = { slot: 1, l: "nfl", id: 6, abbr: "DAL", type: "spread", half: -7, text: "DAL -3.5" };
const openBets = async (page) => {
  await page.locator("#betCard summary").click();
  await expect(page.locator("#betAdd")).toBeVisible();
};

test("the Bets card adds a spread on a team from the picker, and the board shows it on its game", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#betCard")).toBeVisible();
  await expect(page.locator("#betAdd")).toBeHidden();  // folded away until opened
  await openBets(page);
  await expect(page.locator("#betList")).toHaveText("No bets yet.");
  await expect(page.locator("#betline")).toBeHidden();
  await page.locator("#betTeam").click();
  await expect(page.locator(".sheet")).toHaveAttribute("aria-label", "Pick the bet's team");
  // A golf tour or a racing series takes no bet.
  await expect(page.locator(".tabs button", { hasText: "Golf" })).toBeHidden();
  await expect(page.locator(".tabs button", { hasText: "Racing" })).toBeHidden();
  await page.locator("#q").fill("Dallas Cowboys");
  await page.locator("#tiles .tile", { hasText: "Dallas Cowboys" }).click();
  await expect(page.locator("#modal")).not.toHaveClass(/open/);
  await expect(page.locator("#betTeam")).toHaveText("Dallas Cowboys");
  await page.locator("#betLine").fill("-3.5");
  await page.locator("#betAdd").click();
  // Slot 0: the panel puts it in its first empty slot.
  await expectSet(request, { bet: "0,nfl,6,spread,-3.5" });
  await expect(page.locator("#toast")).toHaveText("Added DAL -3.5");
  await expect(page.locator("#betList .betrow label")).toHaveCount(1);
  await expect(page.locator("#betList .betrow label")).toContainText("DAL -3.5");
  await expect(page.locator("#betList .betrow small")).toContainText("DAL vs PHI");
  await expect(page.locator("#betSum")).toHaveText("1 of 8");
  // Before kickoff the board shows the line only.
  await expect(page.locator("#betline .bet")).toHaveText(["DAL -3.5"]);
  // The team stays picked for the next bet: an over on the same game.
  await page.locator("#betType").selectOption("over");
  await expect(page.locator("#betLineHint")).toHaveText("Total points in the game, like 47.5");
  await page.locator("#betLine").fill("47.5");
  await page.locator("#betLine").press("Enter");
  await expectSet(request, { bet: "0,nfl,6,over,47.5" });
  expect((await deviceState(request)).bets.map((b) => b.slot)).toEqual([1, 2]);
  await expect(page.locator("#betline .bet")).toHaveText(["DAL -3.5", "OVER 47.5"]);
});

test("the Bets card takes a bet on a minor league team, found by its level too", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await openBets(page);
  await page.locator("#betTeam").click();
  await expect(page.locator(".tabs button", { hasText: "MiLB" })).toBeVisible();
  await page.locator("#q").fill("triple-a durham");
  await expect(page.locator("#tiles .tile")).toHaveCount(1);
  await page.locator("#tiles .tile", { hasText: "Durham Bulls" }).click();
  await expect(page.locator("#betTeam")).toHaveText("Durham Bulls");
  await page.locator("#betType").selectOption("ml");
  await page.locator("#betAdd").click();
  await expectSet(request, { bet: "0,milb,234,ml," });
  await expect(page.locator("#toast")).toHaveText("Added DUR ML");
  await expect(page.locator("#betList .betrow label")).toContainText("DUR ML");
});

test("the Bets card says what's wrong with a bet before posting it", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await openBets(page);
  const msg = page.locator("#betMsg");
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("Pick the team first.");
  await page.locator("#betTeam").click();
  await page.locator("#q").fill("Buffalo");
  await page.locator("#tiles .tile", { hasText: "Buffalo Bills" }).click();
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("Type the spread, like 3.5.");
  await expect(page.locator("#betLine")).toBeFocused();
  await page.locator("#betLine").fill("3.25");
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("Lines go in whole or half points, like 3 or 3.5.");
  await page.locator("#betLine").fill("three");
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("That isn't a number. Type it like 3.5.");
  await page.locator("#betLine").fill("3,25");
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("Lines go in whole or half points, like 3 or 3.5.");
  await page.locator("#betLine").fill("-120");
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("The panel takes spreads up to 99.5.");
  await page.locator("#betType").selectOption("under");
  await expect(msg).toBeHidden();
  await page.locator("#betLine").fill("-44");
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("A total has no + or -. Type it like 47.5.");
  await page.locator("#betLine").fill("0");
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("A total is more than 0.");
  expect((await sets(request)).filter((q) => q.bet)).toEqual([]);
  // Pick'em is a spread of 0, a moneyline has no line, and "+7.0" is +7.
  await page.locator("#betType").selectOption("spread");
  await page.locator("#betLine").fill("PK");
  await page.locator("#betAdd").click();
  await expectSet(request, { bet: "0,nfl,2,spread,0" });
  await expect(msg).toBeHidden();
  await page.locator("#betType").selectOption("ml");
  await expect(page.locator("#betLineRow")).toBeHidden();
  await page.locator("#betAdd").click();
  await expectSet(request, { bet: "0,nfl,2,ml," });
  await page.locator("#betType").selectOption("spread");
  await page.locator("#betLine").fill("+7.0");
  await page.locator("#betAdd").click();
  await expectSet(request, { bet: "0,nfl,2,spread,+7" });
  await expect(page.locator("#betList .betrow")).toHaveCount(3);
  // A spread with no sign is neither side until the owner says which.
  await page.locator("#betLine").fill("7");
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("Tap \u2212 if the team is the favorite or + if it's the underdog.");
  await page.getByRole("button", { name: "Plus, the underdog" }).click();
  await expect(msg).toBeHidden();
  await page.locator("#betAdd").click();
  await expect(msg).toHaveText("You have that bet already.");
  // A comma for the decimal point, as a phone set to a comma locale types it.
  await page.locator("#betLine").fill("3,5");
  await page.getByRole("button", { name: "Minus, the favorite" }).click();
  await page.locator("#betAdd").click();
  await expectSet(request, { bet: "0,nfl,2,spread,-3.5" });
  await expect(page.locator("#toast")).toHaveText("Added BUF -3.5");
  await page.locator("#betType").selectOption("under");
  await page.locator("#betLine").fill("44,5");
  await page.locator("#betAdd").click();
  await expectSet(request, { bet: "0,nfl,2,under,44.5" });
  expect((await sets(request)).filter((q) => q.bet).length).toBe(5);
});

test("the spread's sign comes from the minus and plus buttons, so no keyboard needs a minus", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await openBets(page);
  await page.locator("#betTeam").click();
  await page.locator("#q").fill("Buffalo");
  await page.locator("#tiles .tile", { hasText: "Buffalo Bills" }).click();
  // The decimal pad an iPhone opens for the line has no minus.
  await expect(page.locator("#betLine")).toHaveAttribute("inputmode", "decimal");
  const minus = page.getByRole("button", { name: "Minus, the favorite" });
  const plus = page.getByRole("button", { name: "Plus, the underdog" });
  await expect(minus).toHaveAttribute("aria-pressed", "false");
  await expect(plus).toHaveAttribute("aria-pressed", "false");
  await page.locator("#betLine").fill("3.5");
  await page.locator("#betAdd").click();
  await expect(page.locator("#betMsg")).toHaveText("Tap \u2212 if the team is the favorite or + if it's the underdog.");
  expect((await sets(request)).filter((q) => q.bet)).toEqual([]);
  await minus.click();
  await expect(minus).toHaveAttribute("aria-pressed", "true");
  await expect(page.locator("#betLine")).toBeFocused();
  await page.locator("#betAdd").click();
  await expectSet(request, { bet: "0,nfl,2,spread,-3.5" });
  await expect(page.locator("#toast")).toHaveText("Added BUF -3.5");
  // The next bet starts with neither side picked.
  await expect(minus).toHaveAttribute("aria-pressed", "false");
  await expect(page.locator("#betLine")).toHaveValue("");
  // A sign typed in lights its button, and the other button changes it.
  await page.locator("#betLine").pressSequentially("-6");
  await expect(minus).toHaveAttribute("aria-pressed", "true");
  await plus.click();
  await expect(page.locator("#betLine")).toHaveValue("+6");
  await expect(plus).toHaveAttribute("aria-pressed", "true");
  await expect(minus).toHaveAttribute("aria-pressed", "false");
  await page.locator("#betAdd").click();
  await expectSet(request, { bet: "0,nfl,2,spread,+6" });
  // A total has no side.
  await page.locator("#betType").selectOption("over");
  await expect(page.locator("#betSign")).toBeHidden();
});

test("a double tap on Add bet adds the bet once", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await openBets(page);
  await page.locator("#betTeam").click();
  await page.locator("#q").fill("Buffalo");
  await page.locator("#tiles .tile", { hasText: "Buffalo Bills" }).click();
  await page.locator("#betType").selectOption("ml");
  await page.locator("#betAdd").dblclick();
  await expectSet(request, { bet: "0,nfl,2,ml," });
  await expect(page.locator("#betList .betrow")).toHaveCount(1);
  await sleep(1000);
  expect((await sets(request)).filter((q) => q.bet)).toEqual([{ bet: "0,nfl,2,ml," }]);
  expect((await deviceState(request)).bets.length).toBe(1);
});

test("two device pages adding a bet at the same time each keep theirs", async ({ page, request, baseURL }) => {
  await reset(request);
  const other = await page.context().newPage();
  await other.route((url) => url.origin !== new URL(baseURL).origin, (route) => route.fulfill({ status: 404, body: "" }));
  await other.route("https://site.api.espn.com/**", (route) => route.fulfill({ json: { events: [] } }));
  for (const p of [page, other]) {
    await p.goto("/");
    await openBets(p);
  }
  // The other page keeps the state from before either bet, as a phone tab
  // just woken up would: it sees slot 1 free.
  const before = await deviceState(request);
  await other.route("**/gameday/state*", (route) => route.fulfill({ json: before }));
  const add = async (p, name) => {
    await p.locator("#betTeam").click();
    await p.locator("#q").fill(name);
    await p.locator("#tiles .tile", { hasText: name }).click();
    await p.locator("#betType").selectOption("ml");
    await p.locator("#betAdd").click();
  };
  // Neither page has seen the other's bet when it adds its own.
  await add(page, "Buffalo Bills");
  await add(other, "Miami Dolphins");
  await expect.poll(async () => (await deviceState(request)).bets.map((b) => b.slot + " " + b.text)).toEqual(["1 BUF ML", "2 MIA ML"]);
  await other.close();
});

test("a full Bets card says to remove one first", async ({ page, request }) => {
  const bets = [1, 2, 3, 4, 5, 6, 7, 8].map((slot) => ({ ...BET, slot, half: -slot * 2, text: "DAL -" + slot, st: "waiting" }));
  await reset(request, { bets });
  await page.goto("/");
  await openBets(page);
  await expect(page.locator("#betSum")).toHaveText("8 of 8");
  await page.locator("#betTeam").click();
  await page.locator("#q").fill("Buffalo");
  await page.locator("#tiles .tile", { hasText: "Buffalo Bills" }).click();
  await page.locator("#betType").selectOption("ml");
  await page.locator("#betAdd").click();
  await expect(page.locator("#betMsg")).toHaveText("All 8 bets are in use. Remove one first.");
  expect((await sets(request)).filter((q) => q.bet)).toEqual([]);
});

test("a bet in a live game shows where it stands on the board and in the card, and can be removed", async ({ page, request }) => {
  await reset(request);
  const g = (await deviceState(request)).game;
  const live = { ...BET, st: "covering", ev: "401772001", kick: g.kick, oa: "PHI", ts: 14, os: 10 };
  const total = { slot: 3, l: "nfl", id: 21, abbr: "PHI", type: "under", half: 47, text: "UNDER 23.5", st: "not_covering",
    ev: "401772001", kick: g.kick, oa: "DAL", ts: 10, os: 14 };
  const waiting = { slot: 5, l: "nba", id: 2, abbr: "BOS", type: "ml", half: 0, text: "BOS ML", st: "waiting" };
  const won = { slot: 6, l: "nfl", id: 21, abbr: "PHI", type: "spread", half: 7, text: "PHI +3.5", st: "won",
    ev: "401771999", kick: g.kick - 7 * 86400, oa: "NYG", ts: 20, os: 21 };
  await reset(request, { game: { ...g, ...LIVE }, bets: [live, total, waiting, won] });
  await page.goto("/");
  await expect(page.locator("#betline .bet")).toHaveText(["DAL -3.5 COVERING", "UNDER 23.5 NOT COVERING"]);
  await expect(page.locator("#betline .bet").first()).toHaveClass(/good/);
  await expect(page.locator("#betline .bet").last()).toHaveClass(/bad/);
  await openBets(page);
  const rows = page.locator("#betList .betrow");
  await expect(rows).toHaveCount(4);
  await expect(rows.nth(0).locator("small")).toHaveText("DAL vs PHI · 14-10");
  await expect(rows.nth(0).locator(".bet")).toHaveText("COVERING");
  await expect(rows.nth(1).locator(".bet")).toHaveText("NOT COVERING");
  await expect(rows.nth(2).locator("small")).toHaveText("Starts when the panel shows a BOS game");
  await expect(rows.nth(2).locator(".bet")).toHaveCount(0);
  await expect(rows.nth(3).locator("small")).toHaveText("PHI vs NYG · Final 20-21");
  await expect(rows.nth(3).locator(".bet")).toHaveText("WON");
  await rows.nth(0).getByRole("button", { name: "Remove DAL -3.5" }).click();
  await expectSet(request, { betdel: "1" });
  await expect(page.locator("#toast")).toHaveText("Removed DAL -3.5");
  await expect(rows).toHaveCount(3);
  await expect(page.locator("#betline .bet")).toHaveText(["UNDER 23.5 NOT COVERING"]);
});

test("firmware without bets has no Bets card", async ({ page, request }) => {
  await reset(request, { bets: null, betmax: null });
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#betCard")).toBeHidden();
  await expect(page.locator("#betline")).toBeHidden();
});

// Favorite players (components/gameday/players.h): the card reads a team's
// roster from ESPN in the browser, posts the player with the name the panel
// shows, and the board lists their stat lines on its game.
const BILLS_ROSTER = {
  team: { id: "2", abbreviation: "BUF" },
  athletes: [
    { position: "offense", items: [
      { id: "3918298", firstName: "Josh", lastName: "Allen", displayName: "Josh Allen", jersey: "17", position: { abbreviation: "QB" } },
      { id: "4379399", firstName: "James", lastName: "Cook", displayName: "James Cook", jersey: "4", position: { abbreviation: "RB" } },
      { id: "2576623", firstName: "Marquez", lastName: "Valdes-Scantling", displayName: "Marquez Valdes-Scantling", jersey: "11", position: { abbreviation: "WR" } },
    ] },
    { position: "specialTeam", items: [
      { id: "3051392", firstName: "Tyler", lastName: "Bass", displayName: "Tyler Bass", jersey: "2", position: { abbreviation: "PK" } },
    ] },
  ],
};
// Flat, as ESPN sends some leagues' rosters, with two brothers.
const DEVILS_ROSTER = {
  athletes: [
    { id: "4565222", firstName: "Jack", lastName: "Hughes", displayName: "Jack Hughes", jersey: "86", position: { abbreviation: "C" } },
    { id: "4697389", firstName: "Luke", lastName: "Hughes", displayName: "Luke Hughes", jersey: "43", position: { abbreviation: "D" } },
    { id: "3114758", firstName: "Nico", lastName: "Hischier", displayName: "Nico Hischier", jersey: "13", position: { abbreviation: "C" } },
    { id: "x", displayName: "Not an id" },
  ],
};
const ALLEN = { l: "nfl", id: 3918298, team: 2, name: "ALLEN" };
const pickPlayerTeam = async (page, name) => {
  await page.locator("#playerTeam").click();
  await expect(page.locator(".sheet")).toHaveAttribute("aria-label", "Pick the player's team");
  // A golf tour or a racing series has no players to pick.
  await expect(page.locator(".tabs button", { hasText: "Golf" })).toBeHidden();
  await expect(page.locator(".tabs button", { hasText: "Racing" })).toBeHidden();
  await page.locator("#q").fill(name);
  await page.locator("#tiles .tile", { hasText: name }).click();
  await expect(page.locator("#modal")).not.toHaveClass(/open/);
  await expect(page.locator("#playerTeam")).toHaveText(name);
};

test("the Favorite players card adds a player from a team's roster and removes one", async ({ page, request }) => {
  await reset(request);
  await page.route("https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/2/roster", (route) => route.fulfill({ json: BILLS_ROSTER }));
  await page.goto("/");
  await expect(page.locator("#playerCard")).toBeVisible();
  await expect(page.locator("#playerList")).toHaveText("No favorite players yet.");
  await expect(page.locator("#playerAdd")).toBeHidden();
  await pickPlayerTeam(page, "Buffalo Bills");
  // Grouped by position: every group's players, by last name.
  await expect(page.locator("#playerPick option")).toHaveText([
    "Josh Allen \u00b7 QB #17", "Tyler Bass \u00b7 PK #2", "James Cook \u00b7 RB #4", "Marquez Valdes-Scantling \u00b7 WR #11",
  ]);
  await page.locator("#playerPick").selectOption("3918298");
  await page.locator("#playerAdd").click();
  // The name the panel shows: the last name in capitals.
  await expectSet(request, { player: "nfl,3918298,2,ALLEN" });
  await expect(page.locator("#toast")).toHaveText("Added Josh Allen");
  await expect(page.locator("#playerList .playerrow label")).toHaveCount(1);
  await expect(page.locator("#playerList .playerrow label")).toContainText("ALLEN");
  await expect(page.locator("#playerList .playerrow small")).toHaveText("Buffalo Bills");
  // Again: the page says so and posts nothing.
  await page.locator("#playerAdd").click();
  await expect(page.locator("#playerMsg")).toHaveText("Josh Allen is a favorite already.");
  // A long name is cut to the panel's 10 characters.
  await page.locator("#playerPick").selectOption("2576623");
  await page.locator("#playerAdd").click();
  await expectSet(request, { player: "nfl,2576623,2,VALDES-SCA" });
  expect((await deviceState(request)).players.map((pl) => pl.name)).toEqual(["ALLEN", "VALDES-SCA"]);
  await page.getByRole("button", { name: "Remove ALLEN" }).click();
  await expectSet(request, { playerdel: "nfl,3918298" });
  await expect(page.locator("#toast")).toHaveText("Removed ALLEN");
  await expect(page.locator("#playerList .playerrow label")).toHaveCount(1);
  await expect(page.locator("#playerList .playerrow label")).toContainText("VALDES-SCA");
});

test("the Favorite players card leaves minor league teams out of its team chooser", async ({ page, request }) => {
  const side = (id, abbr, teamName, score) => ({ team: { id, abbreviation: abbr, teamName }, score });
  await page.route("https://statsapi.mlb.com/**", (route) => route.fulfill({
    json: { dates: [{ games: [
      { gamePk: 2, gameDate: "2026-07-04T23:05:00Z", status: { codedGameState: "I" },
        teams: { away: side(235, "MEM", "Redbirds", 3), home: side(234, "DUR", "Bulls", 2) },
        linescore: { currentInning: 7, inningState: "Bottom", scheduledInnings: 9 } },
    ] }] },
  }));
  // The panel follows a minor league team: the chooser still opens on a tab
  // whose teams have ESPN rosters.
  await reset(request, { team: { l: "milb", id: 234, abbr: "DUR", name: "Durham Bulls" } });
  await page.goto("/");
  await page.locator("#playerTeam").click();
  await expect(page.locator(".sheet")).toHaveAttribute("aria-label", "Pick the player's team");
  await expect(page.locator(".tabs button", { hasText: "MiLB" })).toBeHidden();
  await expect(page.locator(".tabs button", { hasText: "On now" })).toBeVisible();
  // On now lists the minor league game, but neither side picks a roster.
  await page.locator(".tabs button", { hasText: "On now" }).click();
  await expect(page.locator("#tiles .game .side", { hasText: "Bulls" })).toBeDisabled();
  await expect(page.locator("#tiles .game .side", { hasText: "Redbirds" })).toBeDisabled();
  // Search across the tabs finds none of them either.
  await page.locator(".tabs button", { hasText: "MLB" }).first().click();
  for (const q of ["durham", "triple-a", "single-a"]) {
    await page.locator("#q").fill(q);
    await expect(page.locator("#tiles .none")).toHaveText("No team matches");
  }
  await page.getByRole("button", { name: "Close" }).click();
  // Choosing the team still has them.
  await page.locator("#pick").click();
  await page.locator(".tabs button", { hasText: "MiLB" }).click();
  await expect(page.locator("#tiles .tile")).toHaveCount(120);
  await page.locator("#q").fill("durham");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["Durham Bulls"]);
});

test("the Favorite players card says when ESPN's roster can't be read, and tells two brothers apart", async ({ page, request }) => {
  await reset(request);
  let fail = true;
  await page.route("https://site.api.espn.com/apis/site/v2/sports/hockey/nhl/teams/11/roster",
    (route) => (fail ? route.fulfill({ status: 500, body: "" }) : route.fulfill({ json: DEVILS_ROSTER })));
  await page.goto("/");
  await pickPlayerTeam(page, "New Jersey Devils");
  await expect(page.locator("#playerMsg")).toHaveText(
    "Couldn't read the New Jersey Devils roster from ESPN. Check this device's internet connection and try again.");
  await expect(page.locator("#playerAdd")).toBeHidden();
  fail = false;
  await pickPlayerTeam(page, "New Jersey Devils");
  await expect(page.locator("#playerMsg")).toBeHidden();
  // Flat: each player once, the one without an id left out.
  await expect(page.locator("#playerPick option")).toHaveCount(3);
  await page.locator("#playerPick").selectOption("4697389");
  await page.locator("#playerAdd").click();
  // A teammate has the same last name: the initial says which.
  await expectSet(request, { player: "nhl,4697389,11,L. HUGHES" });
  await page.locator("#playerPick").selectOption("3114758");
  await page.locator("#playerAdd").click();
  await expectSet(request, { player: "nhl,3114758,11,HISCHIER" });
  await expect(page.locator("#playerList .playerrow label")).toHaveCount(2);
});

test("a full Favorite players card says to remove one first", async ({ page, request }) => {
  const eight = Array.from({ length: 8 }, (_, i) => ({ l: "nfl", id: 1001 + i, team: 2, name: "P" + (i + 1) }));
  await reset(request, { players: eight });
  await page.route("https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/2/roster", (route) => route.fulfill({ json: BILLS_ROSTER }));
  await page.goto("/");
  await expect(page.locator("#playerList .playerrow label")).toHaveCount(8);
  await pickPlayerTeam(page, "Buffalo Bills");
  await page.locator("#playerAdd").click();
  await expect(page.locator("#playerMsg")).toHaveText("All 8 players are in use. Remove one first.");
  await sleep(300);
  expect((await sets(request)).filter((q) => q.player)).toEqual([]);
});

const SEVEN = Array.from({ length: 7 }, (_, i) => ({ l: "nfl", id: 1001 + i, team: 2, name: "P" + (i + 1) }));
const BILLS_URL = "https://site.api.espn.com/apis/site/v2/sports/football/nfl/teams/2/roster";

test("a player posted counts until the panel lists them, so two quick adds can't both take the last slot", async ({ page, request }) => {
  await reset(request, { players: SEVEN });
  await page.route(BILLS_URL, (route) => route.fulfill({ json: BILLS_ROSTER }));
  await page.goto("/");
  await expect(page.locator("#playerList .playerrow label")).toHaveCount(7);
  await pickPlayerTeam(page, "Buffalo Bills");
  await page.locator("#playerPick").selectOption("3918298");
  await page.locator("#playerAdd").click();
  await page.locator("#playerPick").selectOption("4379399");
  await page.locator("#playerAdd").click();
  await expect(page.locator("#playerMsg")).toHaveText("All 8 players are in use. Remove one first.");
  // "Added" once the panel lists him.
  await expect(page.locator("#toast")).toHaveText("Added Josh Allen");
  await expect(page.locator("#playerList .playerrow label")).toHaveCount(8);
  expect((await sets(request)).filter((q) => q.player)).toEqual([{ player: "nfl,3918298,2,ALLEN" }]);
  expect((await deviceState(request)).players.map((pl) => pl.name)).toEqual(["P1", "P2", "P3", "P4", "P5", "P6", "P7", "ALLEN"]);
});

test("a double tap on Add player posts the player once", async ({ page, request }) => {
  await reset(request);
  await page.route(BILLS_URL, (route) => route.fulfill({ json: BILLS_ROSTER }));
  await page.goto("/");
  await pickPlayerTeam(page, "Buffalo Bills");
  await page.locator("#playerPick").selectOption("3918298");
  await page.locator("#playerAdd").dblclick();
  await expect(page.locator("#toast")).toHaveText("Added Josh Allen");
  await sleep(300);
  expect((await sets(request)).filter((q) => q.player)).toEqual([{ player: "nfl,3918298,2,ALLEN" }]);
  await expect(page.locator("#playerMsg")).toBeHidden();
});

test("a player the panel didn't keep is never shown as added", async ({ page, request }) => {
  await reset(request, { players: SEVEN });
  // Another page takes the last slot; this one still lists seven. Until it
  // posts its own player, the reads it gets are from before that post, so a
  // start-up read that lands late doesn't show it the eighth first.
  let stale = null;
  await page.route("**/gameday/state*", async (route) => {
    const res = await route.fetch();
    await route.fulfill(stale ? { json: stale } : { response: res });
  });
  await page.route("**/gameday/set*", async (route) => {
    stale = null;
    await route.continue();
  });
  await page.route(BILLS_URL, (route) => route.fulfill({ json: BILLS_ROSTER }));
  await page.goto("/");
  await expect(page.locator("#playerList .playerrow label")).toHaveCount(7);
  await pickPlayerTeam(page, "Buffalo Bills");
  stale = await deviceState(request);
  await request.post("/gameday/set?player=" + encodeURIComponent("nfl,4379399,2,COOK"));
  await page.locator("#playerPick").selectOption("3918298");
  await page.locator("#playerAdd").click();
  await expectSet(request, { player: "nfl,3918298,2,ALLEN" });
  await expect(page.locator("#playerMsg")).toHaveText("All 8 players are in use. Remove one first.");
  await expect(page.locator("#playerList .playerrow label").last()).toContainText("COOK");
  expect(await page.locator("#toast").textContent()).not.toBe("Added Josh Allen");
  expect((await deviceState(request)).players.map((pl) => pl.name)).toEqual(["P1", "P2", "P3", "P4", "P5", "P6", "P7", "COOK"]);
});

// Letters the panel folds the way the firmware folds ESPN's words, and
// teammates who share a last name and an initial.
const NAMES_ROSTER = {
  athletes: [
    { id: "203669", firstName: "Martin", lastName: "\u00d8degaard", displayName: "Martin \u00d8degaard", jersey: "8" },
    { id: "5001", firstName: "Ceadach", lastName: "O\u2019Neill", displayName: "Ceadach O\u2019Neill" },
    { id: "5002", firstName: "Rasmus", lastName: "H\u00f8jlund", displayName: "Rasmus H\u00f8jlund" },
    { id: "5003", firstName: "Jeremiah", lastName: "Smith", displayName: "Jeremiah Smith" },
    { id: "5004", firstName: "James", lastName: "Smith", displayName: "James Smith" },
    { id: "5005", firstName: "Elias", lastName: "Pettersson", displayName: "Elias Pettersson" },
    { id: "5006", firstName: "Elias", lastName: "Pettersson", displayName: "Elias N. Pettersson" },
  ],
};

test("the Favorite players card folds letters as the panel does and tells teammates apart", async ({ page, request }) => {
  await reset(request);
  await page.route("https://site.api.espn.com/apis/site/v2/sports/soccer/eng.1/teams/359/roster", (route) => route.fulfill({ json: NAMES_ROSTER }));
  await page.goto("/");
  await pickPlayerTeam(page, "Arsenal");
  const add = async (id, name) => {
    await page.locator("#playerPick").selectOption(id);
    await page.locator("#playerAdd").click();
    await expectSet(request, { player: "epl," + id + ",359," + name });
  };
  await add("203669", "ODEGAARD");
  await add("5001", "O'NEILL");
  await add("5002", "HOJLUND");
  await add("5003", "JE. SMITH");
  await add("5004", "JA. SMITH");
  // The same first name too: nothing in the name tells them apart.
  await add("5005", "E. PETTERS");
  await expect(page.locator("#playerList .playerrow label")).toHaveCount(6);
});

test("the board shows the favorite players' stat lines on its game", async ({ page, request }) => {
  await reset(request);
  const g = (await deviceState(request)).game;
  await reset(request, {
    game: { ...g, ...LIVE, pl: ["PRESCOTT 18/24, 211 YDS, 1 TD", "LAMB 6 REC, 88 YDS"] },
    players: [{ l: "nfl", id: 3059915, team: 6, name: "PRESCOTT" }, { l: "nfl", id: 4241389, team: 6, name: "LAMB" }],
    status: "DAL 14 PHI 10 | 8:12 2nd | PRESCOTT 18/24, 211 YDS, 1 TD | LAMB 6 REC, 88 YDS",
  });
  await page.goto("/");
  await expect(page.locator("#playerline .bet")).toHaveText(["PRESCOTT 18/24, 211 YDS, 1 TD", "LAMB 6 REC, 88 YDS"]);
  await expect(page.locator("#ticker")).toContainText("LAMB 6 REC, 88 YDS");
  // No line while ESPN names none of them.
  await reset(request, { game: { ...g, ...LIVE }, players: [ALLEN] });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(page.locator("#playerline")).toBeHidden();
});

test("firmware without favorite players has no card and no switch for them", async ({ page, request }) => {
  await reset(request, { players: null, playermax: null, team: {}, favs: [{}, {}, {}, {}] });
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.getByRole("switch", { name: "Hockey: Goal", exact: true })).toBeVisible();
  await expect(page.locator("#playerCard")).toBeHidden();
  await expect(page.locator("#playerline")).toBeHidden();
  await expect(page.getByRole("switch", { name: "Favorite players: Scores", exact: true })).toBeHidden();
});
