// Game Day Scoreboard device page.
// Scores and settings come from the gameday component's own routes:
//   GET  /gameday/state           one JSON document with the game and every setting
//   POST /gameday/set?key=value   change settings (team=nfl:6, mode=1, fav2=ncaa:333, tz=3 ...,
//                                 bet=0,nfl,2,spread,-3.5 and betdel=1 for the Bets card,
//                                 player=nfl,3918298,2,ALLEN and playerdel=nfl,3918298 for
//                                 the Favorite players card)
//   POST /gameday/action?do=name  refresh, power_ok (clears the brownout cap)
// The ESPHome event stream (/events) still carries the entities that stay
// for Home Assistant (Power, Brightness, Firmware, WizMote...) and its
// "Game Status" event, sent on every board update, is the cue to re-read the
// state document.
// Bundled with the team table by scripts/build_web.py, which defines
// TEAMS = [[league, espnId, abbr, name], ...], TZS = [[name, iana], ...] and
// the firmware's table for folding accented letters to plain ASCII, FOLD
// (U+00C0..U+017F, a letter each) and FOLD2 ({code point: letters}).

(() => {
  "use strict";

  const $ = (sel, root = document) => root.querySelector(sel);
  const el = (tag, cls, text) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text !== undefined) n.textContent = text;
    return n;
  };
  // A button whose state is a CSS class also says it to screen readers:
  // switches with aria-checked, the other on/off buttons with aria-pressed.
  const setOn = (n, on, attr) => {
    n.classList.toggle("on", on);
    n.setAttribute(attr, String(on));
  };

  // Where the project lives. SITE_URL is the installer and changelog site,
  // the GitHub Pages address today (CONTRIBUTING.md's "Moving to a new
  // address" lists every place it is set); REPO is the GitHub repository.
  const SITE_URL = "https://apolloautomation.github.io/gameday-scoreboard-main";
  const REPO = "ApolloAutomation/gameday-scoreboard-main";

  // The leagues the firmware knows (components/gameday/leagues.h), in tab
  // order. prefix: team list label; tab: picker tab (leagues with the same
  // one share it); name: the long name; path: ESPN's; byId: logo files named
  // by team id; file: one logo for the whole league ("" for none); scan:
  // extra query for today's scoreboard; board: a leaderboard sport ("golf"
  // for a golf tour, "racing" for a racing series), followed like a team but
  // never a favorite, a Live games league or in On now; source: "mlb" for a
  // league whose games and logos come from MLB, not ESPN (minor league
  // baseball), as on the panel, with sport its sport, which ESPN's path
  // gives for the others. scripts/build_web.py checks prefix, path, logo and
  // byId against leagues.h and stops the build when they differ.
  const LEAGUES = {
    nfl: { prefix: "NFL", tab: "NFL", name: "NFL", path: "football/nfl", logo: "nfl", byId: false, scan: "" },
    ncaa: { prefix: "NCAAF", tab: "NCAAF", name: "College football", path: "football/college-football", logo: "ncaa", byId: true, scan: "groups=80&limit=300&", college: true },
    mlb: { prefix: "MLB", tab: "MLB", name: "MLB", path: "baseball/mlb", logo: "mlb", byId: false, scan: "" },
    milb: { prefix: "MiLB", tab: "MiLB", name: "Minor league baseball", path: "mlbstats/milb", logo: "milb", byId: true, scan: "", source: "mlb", sport: "baseball" },
    mls: { prefix: "MLS", tab: "MLS", name: "MLS", path: "soccer/usa.1", logo: "soccer", byId: true, scan: "" },
    epl: { prefix: "EPL", tab: "EPL", name: "Premier League", path: "soccer/eng.1", logo: "soccer", byId: true, scan: "" },
    nba: { prefix: "NBA", tab: "NBA", name: "NBA", path: "basketball/nba", logo: "nba", byId: false, scan: "" },
    wnba: { prefix: "WNBA", tab: "WNBA", name: "WNBA", path: "basketball/wnba", logo: "wnba", byId: false, scan: "" },
    mcbb: { prefix: "NCAAM", tab: "NCAAM", name: "Men's college basketball", path: "basketball/mens-college-basketball", logo: "ncaa", byId: true, scan: "groups=50&limit=300&", college: true },
    nhl: { prefix: "NHL", tab: "NHL", name: "NHL", path: "hockey/nhl", logo: "nhl", byId: false, scan: "" },
    pga: { prefix: "Golf", tab: "Golf", name: "Golf", path: "golf/pga", logo: "leagues", byId: false, file: "pgatour", scan: "", board: "golf" },
    lpga: { prefix: "Golf", tab: "Golf", name: "Golf", path: "golf/lpga", logo: "leagues", byId: false, file: "lpga", scan: "", board: "golf" },
    f1: { prefix: "Racing", tab: "Racing", name: "Racing", path: "racing/f1", logo: "leagues", byId: false, file: "f1", scan: "", board: "racing" },
    // ESPN has no NASCAR logo (its own icon is a dark flag the page can't show)
    nascar: { prefix: "Racing", tab: "Racing", name: "Racing", path: "racing/nascar-premier", logo: "leagues", byId: false, file: "", scan: "", board: "racing" },
  };
  const lgInfo = (lg) => LEAGUES[lg] || LEAGUES.nfl;
  // A golf tour or a racing series: its board is a leaderboard
  const isBoard = (lg) => !!(LEAGUES[lg] && LEAGUES[lg].board);
  const isRacing = (lg) => !!(LEAGUES[lg] && LEAGUES[lg].board === "racing");
  // Games and logos from MLB's own servers (minor league baseball)
  const isMlb = (lg) => !!(LEAGUES[lg] && LEAGUES[lg].source === "mlb");
  // "football", "baseball": the sport's situation switch and splashes
  const sportOf = (lg) => lgInfo(lg).sport || lgInfo(lg).path.split("/")[0];
  // A pro league whose teams have a seed and a place from ESPN (the Standing
  // setting). A college league's have a rank instead, and minor league teams
  // neither: MLB lists no seed or place for them.
  const isPro = (lg) => !!LEAGUES[lg] && !LEAGUES[lg].board && !LEAGUES[lg].college && !isMlb(lg);
  // "#5 OSU": a college team's rank (1-25) before its abbreviation
  const ranked = (rank, abbr) => (rank ? "#" + rank + " " : "") + (abbr || "");
  // A minor league team's level: the fifth field of its TEAMS row (its MLB
  // Stats sport id, teams.h's group).
  const LEVELS = { 11: "Triple-A", 12: "Double-A", 13: "High-A", 14: "Single-A" };
  const levelOf = (t) => (t && LEVELS[t[4]]) || "";
  // The picker tab a league is on: the first league with the same tab text.
  const tabKey = (lg) => Object.keys(LEAGUES).find((k) => LEAGUES[k].tab === lgInfo(lg).tab) || lg;
  // "" for a league with no logo: its image stays hidden.
  const logoUrl = (league, id, abbr) => {
    const l = lgInfo(league);
    if (l.file === "") return "";
    if (l.source === "mlb") return `https://midfield.mlbstatic.com/v1/team/${id}/spots/64`;
    return `https://a.espncdn.com/i/teamlogos/${l.logo}/500-dark/${l.file || (l.byId ? id : String(abbr).toLowerCase())}.png`;
  };
  const keyOf = (t) => t[0] + ":" + t[1];
  const teamByKey = (key) => TEAMS.find((t) => keyOf(t) === key);
  const refKey = (r) => (r && r.id ? r.l + ":" + r.id : "");

  // ---- state -----------------------------------------------------------
  const ents = {};       // id -> last event payload ({id, name, state, value, option...})
  const byName = {};     // name -> id
  let D = null;          // /gameday/state document as the device last sent it
  let S = null;          // D with the page's unconfirmed posts on top; every render reads this
  let game = null;       // S.game
  let connected = false;
  let downSince = 0;     // page clock when the panel stopped answering, 0 while it answers
  let restart = "";      // what the board says while a reboot or reset runs
  let stopped = false;   // reset into setup: the panel won't be back on this network

  // ---- DOM -------------------------------------------------------------
  document.body.innerHTML = `
  <div class="wrap">
    <header class="top">
      <div class="brand">
        <div class="mark">GD</div>
        <div><h1>Game Day Scoreboard<small id="devname">connecting</small></h1></div>
      </div>
      <div class="conn" id="conn" aria-live="polite"><i></i><span>offline</span></div>
    </header>

    <section class="board empty" id="board">
      <div class="state" id="gstate">WAITING</div>
      <div class="stale" id="stale">data stale</div>
      <div class="row">
        <div class="team" id="tA"><img alt=""><div class="abbr">---</div><div class="rec"></div><div class="pips"><i></i><i></i><i></i></div></div>
        <div class="mid">
          <div class="score"><span id="sA">0</span><span class="dash">-</span><span id="sB">0</span></div>
          <div class="clock" id="clock">&nbsp;</div>
          <div class="down" id="down"></div>
        </div>
        <div class="team" id="tB"><img alt=""><div class="abbr">---</div><div class="rec"></div><div class="pips"><i></i><i></i><i></i></div></div>
      </div>
      <div class="gname" id="gname" hidden></div>
      <div class="golf" id="golf" hidden>
        <div class="gt"><span id="golfSt"></span><span id="golfWhen"></span></div>
        <ol id="golfRows"></ol>
      </div>
      <div class="msg" id="msg" hidden></div>
      <div class="ticker" id="ticker">Waiting for the device</div>
      <div class="gone" id="gone" hidden></div>
      <div class="play" id="play"></div>
      <div class="betline" id="betline" hidden></div>
      <div class="betline playerline" id="playerline" hidden></div>
      <div class="splash" id="splash"></div>
    </section>

    <div class="teambar followbar" id="followbar" hidden>
      <div class="cur"><div><div class="name" id="followName">Follow this game</div><div class="lg" id="followLg"></div></div></div>
      <button class="sw" data-set="follow" aria-label="Follow this game"></button>
    </div>

    <div class="teambar guardbar" id="guard" role="status" hidden>
      <div class="cur"><div><div class="name" id="guardName"></div><div class="lg" id="guardText"></div></div></div>
      <button class="btn" id="guardClear" hidden>Clear</button>
    </div>

    <section class="upnext" id="upnext" hidden>
      <div class="uh">Up next</div>
      <div class="ul" id="upnextList"></div>
    </section>

    <div class="modes" id="modes" role="group" aria-label="Mode">
      <button data-mode="0" aria-pressed="false">My team</button>
      <button data-mode="5" aria-pressed="false">Live games</button>
      <button data-mode="4" aria-pressed="false">Favorite teams</button>
    </div>
    <div class="teambar" id="teambar">
      <div class="cur"><img id="curLogo" alt=""><div><div class="name" id="curName">No team chosen</div><div class="lg" id="curLg">Pick the team the panel should follow</div></div></div>
      <button class="btn primary" id="pick">Change team</button>
    </div>
    <div class="teambar livebar" id="livebar" hidden>
      <div class="cur"><div><div class="name" id="liveName">Following a random game in progress</div><div class="lg" id="liveLg">Moves on when it ends, or after Switch every. Leagues:</div><div class="chips" id="liveChips" role="group" aria-label="Leagues"></div><button class="linkbtn" id="liveAuto" hidden>Back to automatic</button></div></div>
      <div class="stepper"><label for="rotate">Switch every</label><input type="number" id="rotate" min="1" max="30" step="1"><span>min</span></div>
    </div>
    <div class="teambar favbar" id="favbar" hidden>
      <div class="cur"><div><div class="name" id="favName">Cycling through your favorites</div><div class="lg">A favorite's game takes the panel near kickoff. When two overlap, the slot order or Alternate below decides.</div></div></div>
      <div class="favctls">
        <div class="stepper"><label for="lockon">Take over</label><input type="number" id="lockon" min="5" max="120" step="1"><span>min before kickoff</span></div>
        <div class="stepper"><label for="release">After the final</label><select id="release">
          <option value="30">30 s</option><option value="60">1 min</option><option value="120">2 min</option><option value="300">5 min</option>
          <option value="600">10 min</option><option value="900">15 min</option><option value="1800">30 min</option><option value="3600">60 min</option>
        </select></div>
        <div class="stepper"><label for="collide">Two games at once</label><select id="collide">
          <option value="0">Stick with the higher team</option><option value="1">Alternate</option>
        </select><input type="number" id="favrotate" min="1" max="30" step="1" aria-label="Alternate every, in minutes" hidden><span id="favrotateUnit" hidden>min</span></div>
      </div>
    </div>

    <div class="grid">
      <div class="card">
        <h2>Ticker</h2>
        <p class="hint">What scrolls along the bottom of the panel during a game.</p>
        <div id="tickerCtls">
          <div class="ctl" data-sport="football"><label>Down and distance<small>Football: the down, yards to go and where the ball is.</small></label><button class="sw" data-set="down" aria-label="Down and distance"></button></div>
          <div class="ctl" data-sport="baseball"><label>Count and at-bat<small>Baseball: the count, outs, pitcher and batter.</small></label><button class="sw" data-set="count" aria-label="Count and at-bat"></button></div>
          <div class="ctl" data-sport="soccer"><label>Match stats<small>Soccer: goals, cards, possession and shots.</small></label><button class="sw" data-set="match" aria-label="Match stats"></button></div>
          <div class="ctl"><label>Last play<small>ESPN's latest play in every sport. In soccer, the latest goal or card.</small></label><button class="sw" data-set="play" aria-label="Last play"></button></div>
          <div class="ctl"><label>Pre-game odds and TV<small>The line, over/under and TV channel before the start, in every league, when ESPN has them. Golf: the purse before a tournament, and the TV channel during it. Racing: the TV channel.</small></label><button class="sw" data-set="odds" aria-label="Pre-game odds and TV"></button></div>
        </div>
      </div>
      <div class="card">
        <h2>Celebrations</h2>
        <p class="hint">Full-screen splashes for scores and big plays, each with its own switch.</p>
        <div id="celebCtls">
          <div class="ctl"><label>Opponent scores too<small>The other side's scores and big plays. Red cards, overtime and the final show either way.</small></label><button class="sw" data-set="opp" aria-label="Opponent scores too"></button></div>
          <div class="ctl" id="alertFavRow"><label>Favorite teams only<small>In Live games, big-play splashes only for games with your team or a favorite. The victory splash still shows for every game.</small></label><button class="sw" data-set="alertfav" aria-label="Favorite teams only"></button></div>
          <div class="ctl" id="watchRow"><label>Switch to big plays in other games<small>The panel checks the other live games in your Live games leagues, or your favorites' games, about every 45 seconds, and shows one for its big play before coming back, even over the idle display. Not while you follow a game. Uses more data.</small></label><button class="sw" data-set="watch" aria-label="Switch to big plays in other games"></button></div>
          <div class="ctl" id="watchStayRow"><label for="watchstay">Stay on a big play<small>How long the panel stays on the other game before it goes back.</small></label><select id="watchstay">
            <option value="30">30 s</option><option value="60">1 min</option><option value="90">90 s</option><option value="120">2 min</option><option value="180">3 min</option><option value="300">5 min</option>
          </select></div>
          <div class="ctl" id="watchMineRow"><label>Switch from my team<small>Your team's live game gives way only to a favorite's big play, and only with this on.</small></label><button class="sw" data-set="watchmine" aria-label="Switch from my team"></button></div>
          <div id="alertCtls"></div>
        </div>
      </div>
      <div class="card">
        <h2>Panel</h2>
        <div id="panelCtls"></div>
        <div class="ctl" id="standingRow" hidden><label for="standing">Standing next to pro teams<small id="standingHelp">What your team shows where its record goes: the record, its playoff seed, or its place in its division, conference or league, when ESPN has them. The other team keeps its record. College teams show their rank, and minor league teams always their record.</small></label><select id="standing" aria-label="Standing next to pro teams" aria-describedby="standingHelp"><option value="0">Record</option><option value="1">Seed</option><option value="2">Place</option></select></div>
        <div id="idleCtls" hidden>
          <div class="sub">When nothing is on</div>
          <div class="ctl"><label for="idle">Idle display<small id="idleHint"></small></label><select id="idle">
            <option value="0">Next game</option><option value="1">Clock</option><option value="2">Scores</option>
            <option value="3">My team today</option><option value="4">Off or dim</option>
          </select></div>
          <div class="ctl" id="idleDimRow" hidden><label for="idledim">Dim to<small>0 turns the screen off. Power stays on.</small></label><input type="range" id="idledim" min="0" max="255" step="1"><span class="val" id="idledimVal" aria-hidden="true"></span></div>
          <div class="ctl"><label>Night hours<small>Dims the panel overnight and shows the idle display, with the clock in place of the scores or the next game. A game you follow still shows.</small></label><button class="sw" data-set="night" aria-label="Night hours"></button></div>
          <div id="nightRows" hidden>
            <div class="ctl"><label for="nightstart">From</label><input type="time" id="nightstart" step="60"></div>
            <div class="ctl"><label for="nightend">To</label><input type="time" id="nightend" step="60"></div>
            <div class="ctl"><label for="nightbri">Night brightness<small>A Brightness change during the night hours lasts until they end.</small></label><input type="range" id="nightbri" min="1" max="255" step="1"><span class="val" id="nightbriVal" aria-hidden="true"></span></div>
          </div>
          <p class="hint" id="idleNow" role="status"></p>
        </div>
      </div>
      <div class="card">
        <h2>Favorites</h2>
        <p class="hint">Up to 16 teams from any league, not golf tours or racing series yet. In Favorite teams mode the order is the priority. Buttons 1 to 4 on a WizMote remote jump to the first four.</p>
        <div class="ctl"><label>Only today's games<small>The playlist skips favorites that aren't playing today.</small></label><button class="sw" data-set="today" aria-label="Only today's games"></button></div>
        <div id="favCtls"></div>
        <button class="btn" id="favAdd">Add a favorite</button>
      </div>
      <div class="card" id="playerCard" hidden>
        <h2>Favorite players</h2>
        <p class="hint">While a favorite player's game is on the panel and ESPN lists the player among its leaders, the ticker shows their stat line. When they score, the celebration names them. The panel reads nothing more from ESPN for it.</p>
        <div id="playerList"></div>
        <div class="sub">Add a player</div>
        <div class="ctl"><label>Team</label><button class="btn" id="playerTeam">Pick a team</button></div>
        <div class="ctl" id="playerPickRow" hidden><label for="playerPick">Player</label><select id="playerPick"></select></div>
        <p class="betmsg" id="playerMsg" role="alert" hidden></p>
        <button class="btn primary" id="playerAdd" hidden>Add player</button>
      </div>
      <div class="card">
        <h2>Remote</h2>
        <p class="hint">Pair a WizMote: turn on discovery, then press any button on the remote.</p>
        <div class="ctl"><label>Status</label><span class="val" id="wizStatus" style="min-width:0;text-align:left"></span></div>
        <div id="remoteCtls"></div>
        <div class="actions" id="remoteActions" style="margin-top:8px"></div>
      </div>
      <div class="card">
        <h2>Setup</h2>
        <p class="hint">Things you set once.</p>
        <div class="sub">Matrix</div>
        <p class="hint">Click the picture that matches yours. The panel restarts with the new layout.</p>
        <div class="panels" id="panels">
          <button class="panelpick" data-cols="1" aria-label="One 64x64 panel">
            <svg viewBox="0 0 96 48" aria-hidden="true"><rect x="30" y="6" width="36" height="36" rx="3"/></svg>
            <span>One 64x64 panel</span>
          </button>
          <button class="panelpick" data-cols="2" aria-label="One 128x64 panel, or two 64x64 side by side">
            <svg viewBox="0 0 96 48" aria-hidden="true"><rect x="10" y="6" width="36" height="36" rx="3"/><rect x="50" y="6" width="36" height="36" rx="3"/></svg>
            <span>One 128x64 panel</span>
            <small>or two 64x64 side by side</small>
          </button>
          <button class="panelpick" data-cols="3" aria-label="Three 64x64 panels side by side">
            <svg viewBox="0 0 96 48" aria-hidden="true"><rect x="5" y="11" width="26" height="26" rx="2"/><rect x="35" y="11" width="26" height="26" rx="2"/><rect x="65" y="11" width="26" height="26" rx="2"/></svg>
            <span>Three wide</span>
            <small>three 64x64 side by side</small>
          </button>
          <button class="panelpick" data-cols="4" aria-label="Four 64x64 panels side by side">
            <svg viewBox="0 0 96 48" aria-hidden="true"><rect x="2" y="14" width="20" height="20" rx="2"/><rect x="26" y="14" width="20" height="20" rx="2"/><rect x="50" y="14" width="20" height="20" rx="2"/><rect x="74" y="14" width="20" height="20" rx="2"/></svg>
            <span>Four wide</span>
            <small>four 64x64 side by side</small>
          </button>
          <button class="panelpick" data-cols="2x2" aria-label="Four 64x64 panels in two rows of two">
            <svg viewBox="0 0 96 48" aria-hidden="true"><rect x="27" y="2" width="20" height="20" rx="2"/><rect x="49" y="2" width="20" height="20" rx="2"/><rect x="27" y="26" width="20" height="20" rx="2"/><rect x="49" y="26" width="20" height="20" rx="2"/></svg>
            <span>Two rows of two</span>
            <small>four 64x64, 128x128</small>
          </button>
        </div>
        <div class="ctl" id="wiringRow" hidden>
          <label for="wiringSel">How the rows are mounted<small>The panel with the controller and the one it feeds are the top row. If part of the board shows upside down, try another choice.</small></label>
          <select id="wiringSel">
            <option value="0">Bottom row upside down</option>
            <option value="2">Top row upside down</option>
            <option value="1">All upright</option>
          </select>
        </div>
        <p class="hint">With three or four panels, set up Wi-Fi through the panel's hotspot: Bluetooth setup is off to leave memory for the panels. Use a 5 V supply rated for all of them.</p>
        <div class="sub">Time</div>
        <div class="tzline" id="tzline">Timezone not known yet</div>
        <div class="ctl" id="tzpick" hidden><label for="tzsel">Timezone</label><select id="tzsel"></select></div>
        <div class="sub">Startup</div>
        <div class="ctl">
          <label>Show how to connect when the panel starts<small>After it joins Wi-Fi: a QR code that opens this page, then the page address, for about 18 seconds, or until you pick a team on a new panel. Then the IP address and .local name lead the ticker for 30 seconds. During a live game the scoreboard comes straight up, with the address in the ticker. Turn it off to go straight to the scoreboard once a team is picked. Hold the boot button any time to show the address again.</small></label>
          <button class="sw" data-set="bootaddr" aria-label="Show how to connect when the panel starts"></button>
        </div>
      </div>
      <div class="card">
        <h2>Device</h2>
        <div class="actions" id="actions"></div>
        <div class="fw" id="fw" hidden>
          <div class="fwline"><span id="fwText">Firmware</span><button class="btn primary" id="fwInstall" hidden>Install</button></div>
          <div class="fwsub" id="fwSub"></div>
        </div>
        <div class="ctl" id="autoupdRow" hidden>
          <label>Install updates automatically<small>Between 3 and 5 AM, never during a live game. Off: the panel only tells you an update is out.</small></label>
          <button class="sw" data-set="autoupd" aria-label="Install updates automatically"></button>
        </div>
        <div class="ctl" id="betaRow" hidden>
          <label>Follow beta<small>Get test versions of the next release before everyone else. They can have bugs. Turned off, the panel stays on its beta until the next release.</small></label>
          <button class="sw" data-set="beta" aria-label="Follow beta"></button>
        </div>
        <div class="ctl" id="barRow" hidden>
          <label>Bar beta<small>This panel installs test versions on its own, between 3 and 5 AM and never during a live game.</small></label>
          <a class="btn" id="barReport" href="#">Something wrong?</a>
        </div>
        <div id="netsCard" hidden>
          <div class="sub">Wi-Fi networks</div>
          <p class="hint">The panel joins the first of these it can see, the last one that worked first. A network you set it up on, through its Game Day hotspot or the installer, is added here.</p>
          <div id="nets"></div>
          <p class="hint" id="netsEmpty" hidden>No saved networks.</p>
        </div>
        <div class="ctl" style="margin-top:8px"><label>Address</label><span class="val" id="ip"></span></div>
        <div class="ctl"><label>Name</label><span class="val" id="hostname"></span></div>
        <div class="sub">Diagnostics</div>
        <div id="health" hidden>
          <div class="ctl"><label>Last restart<small id="hBoots"></small></label><span class="val" id="hStart"></span></div>
          <div class="ctl"><label>Trouble since power on<small>Crashes, freezes and power dips</small></label><span class="val" id="hFault"></span></div>
          <div class="ctl"><label>Free memory</label><span class="val" id="hHeap"></span></div>
          <div class="ctl" id="hTempRow" hidden><label>Chip temperature<small>Above 75 C the panel dims until it cools</small></label><span class="val" id="hTemp"></span></div>
          <div class="ctl" id="hPowerRow" hidden><label>Power dips<small>Brownout restarts since the panel was made</small></label><span class="val" id="hPower"></span></div>
          <div class="ctl"><label>Last ESPN read</label><span class="val" id="hEspn"></span></div>
          <div class="ctl" id="hMlbRow" hidden><label>Last MLB read<small>Minor league baseball</small></label><span class="val" id="hMlb"></span></div>
          <div class="ctl"><label>Firmware</label><span class="val" id="hFw"></span></div>
        </div>
        <div class="ctl"><label>Signal</label><span class="val" id="rssi"></span></div>
        <div id="wifiDiag" hidden>
          <p class="weak" id="wifiWeak" role="status" hidden>Weak signal. Move the panel closer to the router, or the router closer to the panel.</p>
          <div class="spark" id="wifiSparkBox" hidden>
            <svg id="wifiSpark" viewBox="0 0 288 48" preserveAspectRatio="none" role="img"></svg>
            <div class="sparklbl"><span id="wifiFrom"></span><span>Dashed: weak below -75 dBm</span><span>Now</span></div>
          </div>
          <div class="ctl"><label>Wi-Fi drops<small id="wifiLast"></small></label><span class="val" id="wifiDrops"></span></div>
        </div>
        <div class="ctl"><label>Free PSRAM</label><span class="val" id="psram"></span></div>
        <div class="ctl"><label>Data used<small>Scores and logos from ESPN and MLB since the panel started</small></label><span class="val" id="espnData"></span></div>
      </div>
      <details class="card betcard" id="betCard" hidden>
        <summary><h2>Bets</h2><span class="val" id="betSum"></span></summary>
        <p class="hint">Bets you've placed, typed in here by hand: the panel never connects to a sportsbook. The ticker says whether each one is covering during the game, and the result after the final until midnight. The panel follows bets only on games it's already showing: the game on the board, and in Favorite teams your favorites' games.</p>
        <div id="betList"></div>
        <div class="sub">Add a bet</div>
        <div class="ctl"><label>Team<small>For a total, either team in the game</small></label><button class="btn" id="betTeam">Pick a team</button></div>
        <div class="ctl"><label for="betType">Bet</label><select id="betType">
          <option value="spread">Point spread</option><option value="over">Total, over</option><option value="under">Total, under</option><option value="ml">Moneyline</option>
        </select></div>
        <div class="ctl" id="betLineRow"><label for="betLine">Line<small id="betLineHint"></small></label><div class="betlinebox">
          <span class="betsign" id="betSign" role="group" aria-label="Favorite or underdog"><button type="button" data-sign="-" aria-pressed="false" aria-label="Minus, the favorite">&minus;</button><button type="button" data-sign="+" aria-pressed="false" aria-label="Plus, the underdog">+</button></span>
          <input id="betLine" inputmode="decimal" autocomplete="off"></div></div>
        <p class="betmsg" id="betMsg" role="alert" hidden></p>
        <button class="btn primary" id="betAdd">Add bet</button>
      </details>
    </div>

    <p class="foot">
      <span id="ver"></span><span>·</span>
      <a href="https://github.com/${REPO}" target="_blank" rel="noopener">gameday-scoreboard</a>
      <span>·</span><span>scores from ESPN and MLB</span>
    </p>
  </div>

  <div class="modal" id="modal">
    <div class="sheet" role="dialog" aria-modal="true" aria-label="Choose a team">
      <div class="head">
        <input type="search" id="q" placeholder="Search teams" aria-label="Search teams" autocomplete="off">
        <button class="btn" id="close">Close</button>
        <div class="tabs" role="tablist" aria-label="Leagues"><button data-lg="now" class="on" role="tab" aria-selected="true" aria-controls="tiles">On now</button></div>
      </div>
      <div class="tiles" id="tiles" role="tabpanel"></div>
    </div>
  </div>
  <div class="toast" id="toast" role="status"></div>`;

  // ---- helpers ---------------------------------------------------------
  let toastTimer;
  const toast = (t) => {
    const n = $("#toast");
    n.textContent = t;
    n.classList.add("on");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => n.classList.remove("on"), 1800);
  };

  // Entity ids come as "domain/Name" (ESPHome 2026.x) or "domain-object_id"
  // (older builds). Control URLs are /{domain}/{entity name}/{action}, matched
  // against the entity's name, so the name is what goes in the URL.
  const domainOf = (e) => e.domain || String(e.id || "").split(/[\/-]/)[0];
  const post = async (id, action, params, attempt = 0) => {
    const e = ents[id] || { id };
    const domain = domainOf(e);
    const target = e.name || String(id).split("/")[1] || String(id).split("-").slice(1).join("-");
    const qs = params
      ? "?" + Object.entries(params).map(([k, v]) => k + "=" + encodeURIComponent(v)).join("&")
      : "";
    try {
      const r = await fetch(`/${domain}/${encodeURIComponent(target)}/${action}${qs}`, { method: "POST" });
      if (!r.ok) throw new Error("HTTP " + r.status);
      return true;
    } catch (e) {
      if (attempt < 1) {
        // The device only has a handful of sockets; a busy moment is normal.
        await new Promise((res) => setTimeout(res, 500));
        return post(id, action, params, attempt + 1);
      }
      toast("Device did not respond (" + (e.message || "network") + "). Try again.");
      return false;
    }
  };

  // ---- gameday routes ----------------------------------------------------
  const qs = (params) => Object.entries(params).map(([k, v]) => k + "=" + encodeURIComponent(v)).join("&");
  // A tap redraws from what it posted, not from the device's last answer: a
  // state read that left before the device applied the post still carries
  // the old value. Each posted setting is held here until a read shows it,
  // or a read that left after the device answered the post (APPLY_MS for
  // its main loop to apply it) comes back. Then the device's value wins.
  const APPLY_MS = 300;
  const held = {};       // key -> {v: the value as the state document has it, ack: when the post was answered}
  const ref = (key) => ({ l: key.split(":")[0], id: Number(key.split(":")[1]) });
  const stateForm = {
    mode: Number, rotate: Number, lockon: Number, release: Number, collide: Number, standing: Number,
    down: (v) => v === "1", count: (v) => v === "1", match: (v) => v === "1", play: (v) => v === "1", odds: (v) => v === "1", opp: (v) => v === "1",
    bootaddr: (v) => v === "1", today: (v) => v === "1", autoupd: (v) => v === "1", beta: (v) => v === "1",
    follow: (v) => v === "1",
    alerts: Number, alertfav: (v) => v === "1",
    idle: Number, idledim: Number, nightbri: Number, night: (v) => v === "1",
    nightstart: String, nightend: String,
    watch: (v) => v === "1", watchstay: Number, watchmine: (v) => v === "1",
    live: (v) => v.split(",").filter(Boolean),
    team: (v) => ref(v),
    favs: (v) => v.split(",").filter(Boolean).map(ref),
  };
  const sameForm = {
    mode: (a, b) => (a || 0) === (b || 0),
    team: (a, b) => refKey(a) === refKey(b),
    live: (a, b) => (a || []).slice().sort().join(",") === (b || []).slice().sort().join(","),
    favs: (a, b) => (a || []).map(refKey).filter(Boolean).join(",") === (b || []).map(refKey).filter(Boolean).join(","),
  };
  const same = (k, a, b) => (sameForm[k] ? sameForm[k](a, b) : a === b);
  const overlay = () => {
    if (!D) return;
    S = Object.assign({}, D);
    for (const k of Object.keys(held)) S[k] = held[k].v;
    game = S.game || null;
  };

  let stateTimer = null;
  let stateLeft = 0;     // when the read the page last drew left, for posts it must reflect
  let stateInflight = false;
  let stateAgain = false;
  const fetchState = async () => {
    if (stopped) return;
    // A read asked for during another one runs after it, so it sees the latest post.
    if (stateInflight) { stateAgain = true; return; }
    stateInflight = true;
    const left = Date.now();
    // A panel that lost power never closes the connection, so give up on it.
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), 8000);
    let reached = false;
    try {
      const r = await fetch("/gameday/state?_=" + left, { cache: "no-store", signal: ctl.signal });
      reached = true;
      answered();
      // The event stream retries every 30 seconds, and never after an HTTP
      // error. The panel is back, so reopen it now.
      if (!connected && !restart && Date.now() - streamAt > 10000) connect();
      if (r.ok) {
        D = await r.json();
        stateLeft = left;
        for (const k of Object.keys(held)) {
          const h = held[k];
          if (h.ack && (same(k, D[k], h.v) || left > h.ack + APPLY_MS)) delete held[k];
        }
        overlay();
        renderAll();
        if (Object.values(held).some((h) => h.ack)) scheduleState(APPLY_MS + 100);
      }
    } catch (_) {
      // The poll comes back around.
      if (!reached) unanswered();
    }
    clearTimeout(timer);
    stateInflight = false;
    if (stateAgain) { stateAgain = false; fetchState(); }
  };
  const scheduleState = (ms) => { clearTimeout(stateTimer); stateTimer = setTimeout(fetchState, ms); };
  const postSet = async (params, attempt = 0) => {
    try {
      const r = await fetch("/gameday/set?" + qs(params), { method: "POST" });
      if (!r.ok) throw new Error("HTTP " + r.status);
      return true;
    } catch (e) {
      if (attempt < 1) {
        await new Promise((res) => setTimeout(res, 500));
        return postSet(params, attempt + 1);
      }
      toast("Device did not respond (" + (e.message || "network") + "). Try again.");
      return false;
    }
  };
  // Settings post. Posts go one at a time, in tap order, so a slow one (or its
  // retry) never lands after a later one. The device applies them on its main
  // loop, so re-read shortly after.
  let setQueue = Promise.resolve();
  const setGD = (params) => {
    const mine = {};
    for (const k of Object.keys(params)) {
      if (!stateForm[k]) continue;
      mine[k] = held[k] = { v: stateForm[k](String(params[k])), ack: 0 };
    }
    // A league list turns automatic off and an empty one turns it back on.
    // The automatic list itself is the device's to work out, so hold only the flag.
    if (mine.live) {
      mine.live_auto = held.live_auto = { v: !mine.live.v.length, ack: 0 };
      if (!mine.live.v.length) { delete mine.live; delete held.live; }
    }
    if (Object.keys(mine).length) { overlay(); renderAll(); }
    const run = setQueue.then(() => postSet(params)).then((ok) => {
      for (const k of Object.keys(mine)) {
        if (held[k] !== mine[k]) continue;  // a later tap posted this key again
        if (ok) held[k].ack = Date.now();
        else delete held[k];
      }
      if (!ok) { overlay(); renderAll(); }
      scheduleState(APPLY_MS + 100);
      return ok;
    });
    setQueue = run;
    return run;
  };
  const doAction = async (name) => {
    try {
      const r = await fetch("/gameday/action?do=" + encodeURIComponent(name), { method: "POST" });
      if (!r.ok) throw new Error("HTTP " + r.status);
    } catch (e) {
      toast("Device did not respond (" + (e.message || "network") + ")");
    }
  };

  // Every ping calls this; #conn is a live region, so only a flip touches it.
  const setConn = (on) => {
    if (on === connected) return;
    connected = on;
    const c = $("#conn");
    c.classList.toggle("on", on);
    c.querySelector("span").textContent = on ? "live" : "offline";
  };

  // The panel's own miss count can't say it has gone, so the page keeps its
  // own: a state read that gets no answer while the stream is down, or two in
  // a row while the stream still looks open (a panel that lost power).
  let readFails = 0;
  let firstFailAt = 0;
  const renderGone = () => {
    const line = $("#gone");
    line.textContent = restart
      || (downSince ? "Panel not answering since " + new Date(downSince).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }) : "");
    line.hidden = !line.textContent;
    $("#board").classList.toggle("dim", !line.hidden);
  };
  const answered = () => {
    readFails = 0;
    if (downSince) { downSince = 0; renderGone(); }
  };
  const unanswered = () => {
    if (!readFails++) firstFailAt = Date.now();
    if (downSince || (connected && readFails < 2)) return;
    downSince = firstFailAt;
    setConn(false);
    renderGone();
  };

  // ---- board rendering ---------------------------------------------------
  // ESPN also ends a postponed, suspended or canceled game in POST, without
  // marking it done; its detail says which.
  const endedAs = (g) => (g.done === false && g.detail ? g.detail : "Final");

  // The rank stays on the abbreviation's line only where the two fit the
  // team's column, as on the panel: a phone's narrow board leaves it out.
  const fitRank = (node) => {
    const rk = node.querySelector(".abbr .rk");
    if (!rk) return;
    rk.hidden = false;
    rk.hidden = node.querySelector(".abbr").scrollWidth > node.clientWidth;
  };
  window.addEventListener("resize", () => document.querySelectorAll("#board .team").forEach(fitRank));
  const renderTeam = (node, league, id, abbr, rec, timeouts, poss, color, rank) => {
    const img = node.querySelector("img");
    // No id: a side with no team yet, or the demo build's made-up teams.
    const src = id ? logoUrl(league, id, abbr) : "";
    if (img.getAttribute("src") !== src) img.src = src;
    img.style.visibility = src ? "visible" : "hidden";
    const a = node.querySelector(".abbr");
    a.textContent = abbr || "---";
    // A college team's rank before its abbreviation, like a broadcast
    if (rank && abbr) a.prepend(el("span", "rk", "#" + rank), " ");
    fitRank(node);
    a.classList.toggle("poss", !!poss);
    a.style.color = "#" + (color || "ffffff");
    node.querySelector(".rec").textContent = rec || "";
    // Timeout pips are football's (other sports send no timeouts). -1 is a
    // count ESPN didn't send, which hides them rather than showing them used.
    node.querySelector(".pips").style.display = timeouts === null || timeouts < 0 ? "none" : "";
    node.querySelectorAll(".pips i").forEach((p, i) => p.classList.toggle("on", i < (timeouts || 0)));
  };
  // The situation line under the clock: down and distance for football, the
  // count and outs for baseball, the latest goal or a playoff series otherwise.
  const situation = (g) => {
    if (!g.sport) return g.d || "";
    if (g.sport === 1 && g.outs >= 0) return (g.sit ? g.sit + ", " : "") + g.outs + (g.outs === 1 ? " out" : " outs");
    return g.sit || "";
  };

  // An event's days, "Nov 12-15" or "Oct 29-Nov 1", in the panel's zone: a
  // golf event's first and last day (midnight Eastern, read at noon Eastern,
  // shift 43200) or a race weekend's (real times, shift 0).
  const golfDates = (start, end, shift = 43200) => {
    if (!start) return "";
    const zone = (S && S.tz_iana) || undefined;
    const part = (t) => {
      try {
        const p = new Intl.DateTimeFormat("en-US", { timeZone: zone, month: "short", day: "numeric" }).formatToParts(new Date((t + shift) * 1000));
        return { m: p.find((x) => x.type === "month").value, d: p.find((x) => x.type === "day").value };
      } catch (_) { return { m: "", d: "" }; }
    };
    const a = part(start), z = part(end || start);
    if (a.m === z.m && a.d === z.d) return a.m + " " + a.d;
    return a.m + " " + a.d + "-" + (a.m === z.m ? "" : z.m + " ") + z.d;
  };
  // Golf (sport 5): the tour's leaderboard in place of the two teams.
  const renderGolf = (g) => {
    const golf = g.golf || {};
    const st = $("#gstate");
    const ph = golf.ph || 0;  // 0 before, 1 round on, 2 between rounds, 3 final, 4 canceled
    st.classList.toggle("live", ph === 1);
    st.textContent = ph === 1 ? "LIVE" : ph === 3 ? "FINAL" : ph === 0 ? "UPCOMING" : String(golf.st || "").toUpperCase();
    $("#golfSt").textContent = ph === 0 ? "" : golf.st || "";
    const next = golf.next;
    $("#golfWhen").textContent = ph === 0 ? golfDates(golf.start, golf.end)
      : next && next.name ? "Next: " + next.name + ", " + golfDates(next.start, next.end) : "";
    const ol = $("#golfRows");
    ol.innerHTML = "";
    for (const r of golf.rows || []) {
      const li = el("li", r.w ? "w" : null);
      li.appendChild(el("span", "p", r.p || ""));
      li.appendChild(el("span", "n", r.n || ""));
      const sc = String(r.s || "");
      li.appendChild(el("span", "s " + (sc[0] === "-" ? "u" : sc[0] === "+" ? "o" : sc === "E" ? "e" : ""), sc));
      li.appendChild(el("span", "t", r.t || ""));
      ol.appendChild(li);
    }
  };
  // "Sat 9:30 AM" in the panel's zone
  const dayTime = (t) => {
    if (!t) return "";
    try {
      return new Intl.DateTimeFormat("en-US", { timeZone: (S && S.tz_iana) || undefined, weekday: "short", hour: "numeric", minute: "2-digit" }).format(new Date(t * 1000));
    } catch (_) { return ""; }
  };
  // Racing (sport 6): the session's order in golf's place, the car numbers
  // and gaps in its score and thru columns.
  const renderRace = (g) => {
    const race = g.race || {};
    const st = $("#gstate");
    const ph = race.ph || 0;  // 0 before, 1 on track, 2 between sessions, 3 the race's final, 4 canceled
    st.classList.toggle("live", ph === 1);
    st.textContent = ph === 1 ? "LIVE" : ph === 3 ? "FINAL" : ph === 0 ? "UPCOMING" : String(race.st || "").toUpperCase();
    $("#golfSt").textContent = ph === 0 ? "" : race.st || "";
    const nx = race.nx ? race.nx + ", " + dayTime(race.at) : "";
    const next = race.next;
    $("#golfWhen").textContent = ph === 0 ? nx || golfDates(race.start, race.end, 0)
      : ph === 1 || ph === 2 ? (nx ? "Next: " + nx : "")
        : next && next.name ? "Next: " + next.name + ", " + golfDates(next.start, next.end, 0) : "";
    const ol = $("#golfRows");
    ol.innerHTML = "";
    for (const r of race.rows || []) {
      const li = el("li", r.w ? "w" : null);
      li.appendChild(el("span", "p", r.p || ""));
      li.appendChild(el("span", "n", r.n || ""));
      li.appendChild(el("span", "s", r.v || ""));
      li.appendChild(el("span", "t", r.d || ""));
      ol.appendChild(li);
    }
  };

  const renderBoard = () => {
    const b = $("#board");
    const g = game;
    // A leaderboard (golf, sport 5; racing, 6) in place of the two teams
    const lboard = !!(g && g.s !== "NOT_FOUND" && (g.sport === 5 || g.sport === 6));
    b.classList.toggle("golfing", lboard);
    $("#golf").hidden = !lboard;
    $("#golf").classList.toggle("race", lboard && g.sport === 6);
    b.querySelector(".row").hidden = lboard;
    const st = $("#gstate");
    const msg = $("#msg");
    // misses is top level in the state document, not in game. Three in a row is
    // where the firmware adds "no update" to the ticker, with or without a game.
    // captive: the panel's Wi-Fi answered with a sign-in page, which the
    // panel can't get past, so say how to fix it rather than "data stale".
    const captive = !!(S && S.captive);
    const stale = $("#stale");
    stale.textContent = captive ? "Wi-Fi needs a sign-in" : "data stale";
    stale.classList.toggle("on", captive || ((S && S.misses) || 0) >= 3);
    // Bowl or playoff name, when ESPN gives the game one
    const gname = $("#gname");
    gname.textContent = (g && g.s !== "NOT_FOUND" && g.gn) || "";
    gname.hidden = !gname.textContent;
    if (!g || g.s === "NOT_FOUND") {
      b.classList.add("empty");
      st.textContent = g ? "NO GAME" : "WAITING";
      st.classList.remove("live");
      msg.hidden = false;
      const mode = S ? String(S.mode || 0) : "0";
      const favMode = mode === "4" && S && (S.favs || []).some(refKey);
      // With no team picked the panel runs the live board, unless Favorites
      // has teams. Favorites with none follows the team, like My team.
      const liveMode = S && !favMode && (isLive(mode) || S.recap || !(S.team && S.team.id));
      // why is the panel's own reason ("Can't reach ESPN", "Getting ready"),
      // sent only when its board says something other than its default.
      msg.textContent = !g ? "Waiting for the first fetch"
        : captive ? "This Wi-Fi needs a sign-in. Use a home network or a hotspot."
        : g.why ? g.why
        : favMode ? "None of your favorites has a game coming up"
          : liveMode ? "No live games right now"
            : S && S.team && isRacing(S.team.l) ? "No upcoming race for this series"
              : S && S.team && isBoard(S.team.l) ? "No upcoming event for this tour" : "No upcoming game for this team";
      $("#clock").innerHTML = "&nbsp;";
      $("#down").textContent = "";
      return;
    }
    b.classList.remove("empty");
    msg.hidden = true;
    if (lboard) {
      if (g.sport === 6) renderRace(g);
      else renderGolf(g);
      $("#clock").innerHTML = "&nbsp;";
      $("#down").textContent = "";
      return;
    }
    st.classList.toggle("live", g.s === "IN");
    st.textContent = g.s === "IN" ? "LIVE" : g.s === "POST" ? endedAs(g).toUpperCase() : "UPCOMING";
    const football = !g.sport;
    renderTeam($("#tA"), g.l, g.ti, g.ta, g.tr, football ? g.tt : null, g.p === 1, g.tc, g.trk);
    renderTeam($("#tB"), g.l, g.oi, g.oa, g.or, football ? g.ot : null, g.p === 2, g.oc, g.ork);
    $("#sA").textContent = g.ts;
    $("#sB").textContent = g.os;
    const clock = $("#clock");
    if (g.s === "IN") clock.textContent = g.c || "In progress";
    else if (g.s === "POST") clock.textContent = endedAs(g);
    else clock.textContent = g.k || "Upcoming";
    const d = $("#down");
    d.textContent = g.s === "IN" ? situation(g) : g.s === "PRE" && g.tv ? "on " + g.tv : "";
    d.classList.toggle("rz", g.s === "IN" && !!g.rz);
  };

  // ---- mode pills ----------------------------------------------------------------
  // Device modes: 0 my team, 4 favorite teams, 5 live games in the chosen
  // leagues. 1-3 are the older football-only live modes; the page shows them
  // as Live games and a chip tap moves the panel to mode 5.
  const isLive = (m) => m === "1" || m === "2" || m === "3" || m === "5";
  const modeButtons = Array.from(document.querySelectorAll("#modes button"));
  modeButtons.forEach((b) => {
    b.onclick = () => {
      const cur = S ? String(S.mode || 0) : "";
      if (cur === b.dataset.mode || (b.dataset.mode === "5" && isLive(cur))) return;
      setGD({ mode: b.dataset.mode });
      toast(b.textContent);
    };
  });
  const rotateInput = $("#rotate");
  rotateInput.onchange = () => {
    const v = Math.min(30, Math.max(1, Number(rotateInput.value) || 1));
    rotateInput.value = v;
    setGD({ rotate: v });
  };
  // Favorite teams mode settings (mode 4).
  const lockonInput = $("#lockon");
  lockonInput.onchange = () => {
    const v = Math.min(120, Math.max(5, Number(lockonInput.value) || 15));
    lockonInput.value = v;
    setGD({ lockon: v });
  };
  const releaseSelect = $("#release");
  releaseSelect.onchange = () => setGD({ release: releaseSelect.value });
  // Stay on a big play (components/gameday/watch.h), in seconds.
  const staySelect = $("#watchstay");
  staySelect.onchange = () => setGD({ watchstay: staySelect.value });
  const collideSelect = $("#collide");
  const favRotate = $("#favrotate");
  const showFavRotate = () => {
    const alt = collideSelect.value === "1";
    favRotate.hidden = !alt;
    $("#favrotateUnit").hidden = !alt;
  };
  collideSelect.onchange = () => {
    showFavRotate();
    setGD({ collide: collideSelect.value });
  };
  favRotate.onchange = () => {
    const v = Math.min(30, Math.max(1, Number(favRotate.value) || 1));
    favRotate.value = v;
    setGD({ rotate: v });
  };
  // Leagues Live mode follows. An old football live mode reads as its leagues.
  const liveKeys = () => {
    const m = String(S.mode || 0);
    if (m === "1") return ["nfl"];
    if (m === "2") return ["ncaa"];
    if (m === "3") return ["nfl", "ncaa"];
    return S.live || [];
  };
  // No leagues picked: the device follows the leagues of the team and the
  // favorites, and that list changes by itself when a favorite is added.
  const liveAuto = () => String(S.mode || 0) === "5" && !!S.live_auto;
  $("#liveAuto").onclick = () => {
    setGD({ live: "" });
    toast("Leagues follow your team and favorites");
  };
  const renderLiveBar = () => {
    const auto = liveAuto();
    // With nothing live the device shows the saved team's card, or with no
    // team a rotation of recent finals, and keeps scanning.
    // Follow this game (the bar under the board) holds the game up.
    $("#liveName").textContent = S.fallback ? "No live game right now, showing your team until one starts"
      : S.recap ? "No live game right now, showing recent finals"
        : S.follow ? "Staying on the game you follow"
          : "Following a random game in progress";
    $("#liveLg").textContent = (S.fallback || S.recap || S.follow ? "" : "Moves on when it ends, or after Switch every. ")
      + (auto ? "Leagues follow your team and favorites. Tap a league to choose by hand:" : "Leagues:");
    $("#liveChips").classList.toggle("auto", auto);
    $("#liveAuto").hidden = auto || String(S.mode || 0) !== "5";
    renderLiveChips();
  };
  // One chip per league with teams, built once: a redraw only sets each one's
  // state, so it never takes the keyboard focus off the chip it is on.
  const liveChips = Object.keys(LEAGUES).filter((lg) => !isBoard(lg) && TEAMS.some((t) => t[0] === lg)).map((lg) => {
    const b = el("button", "chip", LEAGUES[lg].tab);
    setOn(b, false, "aria-pressed");
    // From the list as it is now, not as it was when the chips were drawn:
    // a second tap can come before the redraw.
    b.onclick = () => {
      const cur = liveKeys();
      const next = cur.includes(lg) ? cur.filter((k) => k !== lg) : cur.concat([lg]);
      if (!next.length) return toast("Pick at least one league");
      const params = { live: next.join(",") };
      if (String(S.mode || 0) !== "5") params.mode = 5;
      setGD(params);
    };
    $("#liveChips").appendChild(b);
    return [lg, b];
  });
  const renderLiveChips = () => {
    const on = liveKeys();
    for (const [lg, b] of liveChips) setOn(b, on.includes(lg), "aria-pressed");
  };
  // Follow this game: Live games and Favorite teams keep the game on the
  // board instead of moving on, until it ends (Favorite teams: until its
  // final has been up for After the final). The switch posts follow=1 or 0
  // like the others (.sw[data-set]). Not in My team, which follows one team
  // already, nor over Live games' team card or recent finals, a golf or
  // racing board, or a game that has ended and isn't followed already.
  // While another game's big play has the board for a moment (S.visit), the
  // board shows that game, but the switch follows the game the panel was
  // showing, and the panel goes straight back to it: only while that one can
  // still be followed (S.visit_follow), not once it has ended.
  const renderFollow = (mode) => {
    const favMode = mode === "4" && (S.favs || []).some(refKey);
    const liveGame = isLive(mode) && !S.fallback && !S.recap;
    const g = game;
    const on = !!S.follow;
    const up = S.visit
      ? !!S.visit_follow || on
      : !!(g && g.s !== "NOT_FOUND" && (g.s !== "POST" || on) && g.id && g.sport !== 5 && g.sport !== 6);
    // Firmware from before the switch leaves follow out.
    $("#followbar").hidden = S.follow === undefined || !(up && (favMode || liveGame));
    $("#followbar").classList.toggle("on", on);
    $("#followName").textContent = on ? "Following this game" : "Follow this game";
    const until = favMode ? "until its final has been up for After the final" : "until it ends";
    $("#followLg").textContent = on
      ? "The panel stays on this game " + until + ". Turn this off to go back to "
        + (favMode ? "your favorites." : "switching games.")
      : S.visit
        ? "The panel is showing a big play in another game. Turn this on to go straight back to the game it was showing and keep it on the panel " + until + "."
        : "Keep this game on the panel " + until + ", instead of "
          + (favMode ? "moving to another favorite." : "switching to another game.");
  };
  const renderMode = () => {
    if (!S) return;
    const mode = String(S.mode || 0);
    modeButtons.forEach((b) => setOn(b, b.dataset.mode === mode || (b.dataset.mode === "5" && isLive(mode)), "aria-pressed"));
    const live = isLive(mode);
    if (live) renderLiveBar();
    const favs = mode === "4";
    $("#teambar").hidden = live || favs;
    $("#livebar").hidden = !live;
    $("#favbar").hidden = !favs;
    if (document.activeElement !== rotateInput) rotateInput.value = S.rotate;
    if (document.activeElement !== favRotate) favRotate.value = S.rotate;
    if (document.activeElement !== lockonInput) lockonInput.value = S.lockon || 15;
    if (document.activeElement !== releaseSelect) releaseSelect.value = String(S.release || 30);
    if (document.activeElement !== collideSelect) {
      collideSelect.value = String(S.collide || 0);
      showFavRotate();
    }
    if (favs) {
      const set = (S.next || []).filter((n) => n.t && n.t.id).length;
      const shown = (S.next || []).find((n) => n.slot === S.shown);
      $("#favName").textContent = !set
        ? "No favorites set yet"
        : S.follow && shown
          ? "Following " + (shown.t.name || shown.t.abbr)
          : S.locked && shown
            ? "Locked on " + (shown.t.name || shown.t.abbr)
            : "Cycling through your favorites";
    }
    renderFollow(mode);
  };

  // ---- the idle display and the night hours (Panel card) -------------------
  // What the panel shows when no game it follows is on, and overnight. The
  // device keeps the settings (idle, idledim, night, nightstart, nightend,
  // nightbri); each control posts its own key, held until the panel has it.
  const IDLE_HINTS = [
    "What each mode shows anyway: your team's next game, your favorites, or recent scores.",
    "The time in your team's colors, and its next game on a wide panel.",
    "Recent final scores and the games still to come today, for your team's and your favorites' leagues.",
    "On game day the matchup with a countdown to the start, then the final until midnight. Other days, the clock.",
    "The panel goes dark, or as dim as you set below, without turning it off.",
  ];
  const idleSelect = $("#idle");
  idleSelect.onchange = () => setGD({ idle: idleSelect.value });
  const idleDim = $("#idledim");
  idleDim.oninput = () => ($("#idledimVal").textContent = idleDim.value);
  idleDim.onchange = () => setGD({ idledim: idleDim.value });
  const nightBri = $("#nightbri");
  nightBri.oninput = () => ($("#nightbriVal").textContent = nightBri.value);
  nightBri.onchange = () => setGD({ nightbri: nightBri.value });
  // A time field gives "" while it is being cleared: nothing to post then.
  ["nightstart", "nightend"].forEach((key) => {
    const input = $("#" + key);
    input.onchange = () => { if (/^\d\d:\d\d$/.test(input.value)) setGD({ [key]: input.value }); };
  });
  const renderIdle = () => {
    // Firmware from before the setting leaves idle out.
    $("#idleCtls").hidden = !S || S.idle === undefined;
    if ($("#idleCtls").hidden) return;
    const show = Number(S.idle) || 0;
    if (document.activeElement !== idleSelect) idleSelect.value = String(show);
    $("#idleHint").textContent = IDLE_HINTS[show] || "";
    $("#idleDimRow").hidden = show !== 4;
    if (document.activeElement !== idleDim) {
      idleDim.value = S.idledim || 0;
      $("#idledimVal").textContent = String(S.idledim || 0);
    }
    $("#nightRows").hidden = !S.night;
    for (const key of ["nightstart", "nightend"])
      if (document.activeElement !== $("#" + key)) $("#" + key).value = S[key] || "";
    if (document.activeElement !== nightBri) {
      nightBri.value = S.nightbri || 1;
      $("#nightbriVal").textContent = String(S.nightbri || 1);
    }
    // What the panel does about it now
    const now = { clock: "the clock", scores: "recent scores", today: "today's game" }[S.idle_view];
    $("#idleNow").textContent = S.dark ? "The panel is dark until a game you follow starts."
      : now ? "Showing " + now + (S.night_now ? ", in the night hours," : "") + " until a game you follow starts."
        : S.night_now ? "In the night hours." : "";
  };

  // ---- toggles and favorites (static controls on the state document) --------
  const standingSel = $("#standing");
  standingSel.onchange = () => setGD({ standing: standingSel.value });
  document.querySelectorAll(".sw[data-set]").forEach((sw) => {
    sw.setAttribute("role", "switch");
    setOn(sw, false, "aria-checked");
    sw.onclick = () => setGD({ [sw.dataset.set]: S && S[sw.dataset.set] ? 0 : 1 });
  });
  // Big-play splashes (components/gameday/alerts.h): a switch per type, in
  // the order of the bits of the state document's "alerts" mask, by sport.
  const ALERTS = [
    ["football", "Football", [
      ["Touchdown"], ["Field goal"], ["Extra point"],
      ["2-point conversion", "Two points after a touchdown, and any two points that aren't clearly a safety."],
      ["Safety", "Two points ESPN calls a safety, or that come in the middle of a drive."],
      ["Interception"],
      ["Fumble recovery", "Only a fumble the other team recovers."]]],
    ["baseball", "Baseball", [
      ["Runs", "Runs that aren't a home run, and every minor league run: MLB doesn't say how those scored."],
      ["Home run", "MLB games."],
      ["Grand slam", "A home run that scores four, in MLB games."],
      ["Walk-off", "The home team goes ahead in the 9th inning or later, which ends the game. In a minor league doubleheader's 7-inning game, the 7th."]]],
    ["hockey", "Hockey", [["Goal", "Every goal the same way: ESPN doesn't say which came on a power play or into an empty net."]]],
    ["soccer", "Soccer", [["Goal"], ["Penalty goal", "A goal from the penalty spot."], ["Red card", "Either side's, by name."]]],
    ["basketball", "Basketball", [
      ["Lead change", "The team that was behind goes ahead, in the last 2 minutes or in overtime."],
      ["Buzzer beater", "A basket in the last second of a period that ties the game or takes the lead."],
      ["Overtime", "When overtime starts."],
      ["Final", "When the game ends, unless your team won: then the victory splash."]]],
    // Not a sport: shown whenever the firmware has favorite players.
    ["players", "Favorite players", [
      ["Scores", "Names a favorite player who scores, in place of the team's splash. Football, baseball, soccer and hockey; in basketball only a late lead change or a buzzer beater."]]],
  ];
  const alertSws = [];
  for (const [sport, name, kinds] of ALERTS) {
    const group = el("div", "alertSport");
    group.dataset.sport = sport;
    group.append(el("div", "sub", name));
    for (const [label, hint] of kinds) {
      const row = el("div", "ctl");
      const lab = el("label", "", label);
      if (hint) lab.append(el("small", "", hint));
      const sw = el("button", "sw");
      const bit = alertSws.length;
      sw.setAttribute("role", "switch");
      sw.setAttribute("aria-label", name + ": " + label);
      setOn(sw, false, "aria-checked");
      sw.onclick = () => S && S.alerts !== undefined && setGD({ alerts: (S.alerts ^ (1 << bit)) >>> 0 });
      row.append(lab, sw);
      group.append(row);
      alertSws.push(sw);
    }
    $("#alertCtls").append(group);
  }
  // Favorites: one row per team in priority order. Every change posts the
  // whole list (favs=) so the device always sees one ordered list.
  let favKeys = [];
  // Each favorite's slot on the panel, which is its WizMote button for the
  // first four. A slot emptied on its own (fav1=none, as the app can post)
  // leaves a gap the list skips.
  let favSlots = [];
  const favMax = () => (S && S.favmax) || 4;
  // renderSettings() redraws the list from S, which already carries the post.
  const postFavs = (keys) => setGD({ favs: keys.join(",") });
  const renderFavs = () => {
    const host = $("#favCtls");
    // Where the keyboard was, so the rebuilt list puts it back: the same
    // button on the same team's row, or on the row now in a removed one's place.
    const a = document.activeElement;
    const was = a && host.contains(a) && a.closest(".ctl.fav")
      ? { key: a.closest(".ctl.fav").dataset.key, title: a.title, at: Array.from(host.children).indexOf(a.closest(".ctl.fav")) }
      : null;
    host.innerHTML = "";
    favKeys.forEach((key, i) => {
      const t = teamByKey(key);
      const row = el("div", "ctl fav");
      row.dataset.key = key;
      const img = el("img");
      img.loading = "lazy";
      img.alt = "";
      const src = t ? logoUrl(t[0], t[1], t[2]) : "";
      if (src) img.src = src;
      else img.style.visibility = "hidden";
      row.appendChild(img);
      const name = el("label", null, (i + 1) + ". " + (t ? t[3] : key));
      const slot = favSlots[i] ?? i;
      if (t) name.appendChild(el("small", null, lgInfo(t[0]).name + (slot < 4 ? " · remote button " + (slot + 1) : "")));
      row.appendChild(name);
      const ctl = el("span", "favmove");
      const mk = (label, title, fn) => {
        const b = el("button", null, label);
        b.title = title;
        b.setAttribute("aria-label", title);
        b.onclick = fn;
        ctl.appendChild(b);
      };
      const swap = (j) => {
        if (j < 0 || j >= favKeys.length) return;
        const next = favKeys.slice();
        next[i] = favKeys[j];
        next[j] = favKeys[i];
        postFavs(next);
      };
      mk("\u25B2", "Move up", () => swap(i - 1));
      mk("\u25BC", "Move down", () => swap(i + 1));
      mk("\u2715", "Remove", () => postFavs(favKeys.filter((_, k) => k !== i)));
      row.appendChild(ctl);
      host.appendChild(row);
    });
    if (!favKeys.length) host.appendChild(el("p", "hint", "No favorites yet."));
    $("#favAdd").hidden = favKeys.length >= favMax();
    if (was) {
      const rows = Array.from(host.querySelectorAll(".ctl.fav"));
      const row = rows.find((r) => r.dataset.key === was.key) || rows[Math.min(was.at, rows.length - 1)];
      const b = row && Array.from(row.querySelectorAll("button")).find((x) => x.title === was.title);
      (b || $("#favAdd")).focus();
    }
  };
  const addFav = (t) => {
    const key = keyOf(t);
    if (favKeys.includes(key)) return toast(t[3] + " is already a favorite");
    if (favKeys.length >= favMax()) return toast("That's the most favorites the panel holds");
    postFavs(favKeys.concat([key]));
    toast("Added the " + t[3]);
  };
  const renderSettings = () => {
    if (!S) return;
    document.querySelectorAll(".sw[data-set]").forEach((sw) => setOn(sw, !!S[sw.dataset.set], "aria-checked"));
    const keys = [], slots = [];
    (S.favs || []).forEach((t, i) => { if (refKey(t)) { keys.push(refKey(t)); slots.push(i); } });
    if (keys.join(",") !== favKeys.join(",") || slots.join(",") !== favSlots.join(",")) {
      favKeys = keys;
      favSlots = slots;
      renderFavs();
    } else {
      $("#favAdd").hidden = favKeys.length >= favMax();
    }
    // A situation switch for each sport the panel follows (team, favorites,
    // Live games leagues picked by hand), all of them before anything is
    // picked. The automatic Live list is no help here: it never comes back
    // empty (NFL on a new panel). Hockey and basketball have no situation
    // line, so no switch.
    const sports = new Set([S.team].concat(S.favs || []).filter((t) => t && t.id).map((t) => t.l)
      .concat(S.live_auto ? [] : S.live || []).map(sportOf));
    document.querySelectorAll("#tickerCtls .ctl[data-sport], #alertCtls .alertSport").forEach((row) => {
      row.hidden = row.dataset.sport === "players" ? S.players === undefined : sports.size > 0 && !sports.has(row.dataset.sport);
    });
    // Firmware from before big-play alerts has neither.
    $("#alertCtls").hidden = $("#alertFavRow").hidden = S.alerts === undefined;
    alertSws.forEach((sw, bit) => setOn(sw, ((S.alerts >>> bit) & 1) === 1, "aria-checked"));
    // Older firmware sends no standing. The row shows while nothing is
    // followed, or a pro team is (college teams have a rank instead).
    const followed = [S.team].concat(S.favs || []).filter((t) => t && t.id);
    $("#standingRow").hidden = S.standing === undefined || (followed.length > 0 && !followed.some((t) => isPro(t.l)));
    if (document.activeElement !== standingSel) standingSel.value = String(S.standing || 0);
    // Nor the switches for other games' big plays. A stay the list lacks gets its own entry.
    $("#watchRow").hidden = $("#watchStayRow").hidden = $("#watchMineRow").hidden = S.watch === undefined;
    if (document.activeElement !== staySelect && S.watchstay) {
      const v = String(S.watchstay);
      if (![...staySelect.options].some((o) => o.value === v)) staySelect.add(new Option(v + " s", v));
      staySelect.value = v;
    }
    // Firmware that can't install updates (older, or the demo build) leaves it out.
    $("#autoupdRow").hidden = S.autoupd === undefined;
    // Older firmware and the bar and demo builds have no beta to follow.
    $("#betaRow").hidden = S.beta === undefined;
    // The firmware box's rollback line and overnight note read this document,
    // which can land after the update entity does. A check in progress keeps
    // saying so; its answer redraws the box.
    if (!fwChecking && fwInputs() !== fwDrawn && ents[byName["Firmware"]]) renderUpdate(ents[byName["Firmware"]]);
    // The bar build: a note, and an email to us with the panel's name in it.
    $("#barRow").hidden = !S.bar;
    if (S.bar) {
      const who = S.name || "this panel";
      $("#barReport").href = "mailto:" + S.bar.report + "?subject=" + encodeURIComponent("Bar panel " + who) +
        "&body=" + encodeURIComponent("Panel: " + who + "\nWhat's wrong:\n");
    }
    $("#ticker").textContent = S.status || "";
    $("#play").textContent = game && game.lp ? "Last play: " + game.lp : "";
  };

  // ---- panel picker ------------------------------------------------------------
  // Picking a layout restarts the device, after a confirm; the page waits it
  // out. panels is the count wide, rows 2 for the 2x2 (older firmware sends
  // neither rows nor wiring), and wiring how the 2x2's rows are mounted.
  const PANEL_SIZES = { 1: "64x64", 2: "128x64", 3: "192x64", 4: "256x64", "2x2": "128x128" };
  const restartAs = (as) => {
    toast(as);
    waitForReboot("", as + ". This page reconnects when the panel is back.");
    toBoard();
  };
  const renderPanels = () => {
    if (!S || !S.panels) return;
    const cur = (S.rows || 1) > 1 ? S.panels + "x" + S.rows : String(S.panels);
    const picks = document.querySelectorAll("#panels .panelpick");
    const sel = $("#wiringSel");
    picks.forEach((b) => {
      setOn(b, b.dataset.cols === cur, "aria-pressed");
      b.onclick = async () => {
        if (b.dataset.cols === cur) return;
        const size = PANEL_SIZES[b.dataset.cols];
        if (!confirm("Restart the panel as " + size + "?")) return;
        picks.forEach((x) => (x.disabled = true));
        // The 2x2 keeps the wiring saved last; it can be changed once it's back.
        const params = { panels: b.dataset.cols };
        if (b.dataset.cols === "2x2") params.wiring = String(S.wiring || 0);
        // Only a layout the panel took restarts it; a failed post has said so.
        if (!(await setGD(params))) {
          picks.forEach((x) => (x.disabled = false));
          return;
        }
        restartAs("Restarting as " + size);
      };
    });
    // The wiring matters on the 2x2 only, and changing it restarts that.
    $("#wiringRow").hidden = cur !== "2x2" || S.wiring === undefined;
    if (document.activeElement !== sel) sel.value = String(S.wiring || 0);
    sel.onchange = async () => {
      const was = String(S.wiring || 0);
      if (!confirm("Restart the panel with the new wiring?")) {
        sel.value = was;
        return;
      }
      sel.disabled = true;
      const ok = await setGD({ wiring: sel.value });
      sel.disabled = false;
      if (!ok) {
        sel.value = was;
        return;
      }
      restartAs("Restarting with the new wiring");
    };
  };

  // ---- controls --------------------------------------------------------
  const controlDefs = [
    { name: "Power", into: "#panelCtls", label: "Panel on" },
    { name: "Brightness", into: "#panelCtls", label: "Brightness" },
    { name: "Scroll Speed", into: "#panelCtls", label: "Ticker speed" },
    { name: "Select Page", into: "#panelCtls", label: "Showing" },
    { name: "WizMote Auto-Discovery", into: "#remoteCtls", label: "Discovery" },
    { name: "Clear WizMote Pairing", into: "#remoteActions", label: "Unpair remote", danger: true },
    { name: "Refresh Now", into: "#actions", label: "Refresh scores" },
    { name: "Check for Updates", into: "#actions", label: "Check for updates" },
    // restart: "" waits for the panel to come back; otherwise the panel
    // restarts into setup and this says why.
    { name: "Reboot", into: "#actions", label: "Reboot", danger: true, restart: "" },
    { name: "Reset Wi-Fi", into: "#actions", label: "Reset Wi-Fi", danger: true,
      restart: "The panel is restarting into Wi-Fi setup for 15 minutes, so this page won't reconnect. Its settings are kept, and unplugging it and plugging it back in takes it back to the Wi-Fi it already knows." },
    { name: "Factory Reset", into: "#actions", label: "Factory reset", danger: true,
      restart: "The panel is restarting into setup with everything erased, so this page won't reconnect." },
  ];
  const built = {};

  const buildControl = (def, e) => {
    const domain = domainOf(e);
    const host = $(def.into);
    if (domain === "button") {
      const b = el("button", "btn" + (def.danger ? " danger" : ""), def.label);
      b.onclick = async () => {
        if (def.danger && !confirm(def.label + "?")) return;
        const sent = post(e.id, "press");
        if (def.name === "Check for Updates") { checkForUpdates(b); return; }
        toast(def.label);
        if (def.restart === undefined || !(await sent)) return;
        if (def.restart === "") waitForReboot("", "Restarting. This page reconnects when the panel is back.");
        else intoSetup(def.restart);
        toBoard();
      };
      host.appendChild(b);
      return { update() {} };
    }
    const row = el("div", "ctl");
    const label = el("label", null, def.label);
    const id = "ctl-" + def.name.toLowerCase().replace(/[^a-z0-9]+/g, "-");
    row.appendChild(label);
    if (domain === "switch") {
      const sw = el("button", "sw");
      sw.setAttribute("role", "switch");
      sw.setAttribute("aria-label", def.label);
      setOn(sw, false, "aria-checked");
      sw.onclick = () => {
        const on = !sw.classList.contains("on");
        setOn(sw, on, "aria-checked");
        post(e.id, on ? "turn_on" : "turn_off");
      };
      row.appendChild(sw);
      host.appendChild(row);
      return { update(ev) { setOn(sw, ev.value === true || ev.state === "ON", "aria-checked"); } };
    }
    if (domain === "number") {
      const val = el("span", "val");
      val.setAttribute("aria-hidden", "true");
      const r = el("input");
      r.id = label.htmlFor = id;
      r.type = "range";
      r.min = e.min_value ?? 0;
      r.max = e.max_value ?? 100;
      r.step = e.step ?? 1;
      r.oninput = () => (val.textContent = r.value);
      r.onchange = () => post(e.id, "set", { value: r.value });
      row.appendChild(r);
      row.appendChild(val);
      host.appendChild(row);
      return { update(ev) { if (document.activeElement !== r) { r.value = ev.value; val.textContent = ev.value; } } };
    }
    if (domain === "select") {
      const s = el("select");
      s.id = label.htmlFor = id;
      (e.option || []).forEach((o) => s.appendChild(el("option", null, o)));
      s.onchange = () => post(e.id, "set", { option: s.value });
      row.appendChild(s);
      host.appendChild(row);
      return { update(ev) { if (ev.option) { s.innerHTML = ""; ev.option.forEach((o) => s.appendChild(el("option", null, o))); } s.value = ev.value; } };
    }
    return { update() {} };
  };

  // ---- today's games (fetched by the browser, not the device) -------------
  const ESPN = "https://site.api.espn.com/apis/site/v2/sports/";
  // Minor league baseball: a day's games at the four levels, from the MLB
  // Stats API like the panel's Live games scan.
  const MLB_DAY = "https://statsapi.mlb.com/api/v1/schedule?sportId=11,12,13,14&hydrate=team,linescore"
    + "&fields=dates,games,gamePk,gameDate,status,codedGameState,startTimeTBD,teams,away,home,team,id,abbreviation,"
    + "teamName,score,linescore,currentInning,inningState,scheduledInnings&date=";
  const ordinal = (n) => n + (n % 100 >= 11 && n % 100 <= 13 ? "th" : ({ 1: "st", 2: "nd", 3: "rd" }[n % 10] || "th"));
  // One MLB Stats game as On now's tile: "Top 5th", "Final/10", "Postponed".
  // Game 2 of a doubleheader has no start time (startTimeTBD) and a
  // gameDate five minutes after game 1's: "TBD", as the panel shows it.
  const mlbGame = (lg, g) => {
    const c = String((g.status || {}).codedGameState || "S")[0];
    const state = "IMN".includes(c) ? "in" : "FOQR".includes(c) ? "post" : "DCTU".includes(c) ? "off" : "pre";
    const ls = g.linescore || {};
    const half = { Top: "Top", Bottom: "Bot", Middle: "Mid", End: "End" }[ls.inningState];
    const inning = Number(ls.currentInning) || 0;
    const detail = state === "in" ? (half && inning ? half + " " + ordinal(inning) : "In Progress")
      : state === "post" ? (inning && ls.scheduledInnings && inning !== ls.scheduledInnings ? "Final/" + inning : "Final")
      : state === "off" ? ({ D: "Postponed", C: "Canceled" }[c] || "Suspended")
      : (g.status || {}).startTimeTBD ? "TBD"
      : new Date(g.gameDate).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
    const side = (k) => {
      const x = (g.teams || {})[k] || {};
      const t = x.team || {};
      return { id: Number(t.id), abbr: t.abbreviation || "", score: String(x.score ?? 0), name: t.teamName || t.name || "" };
    };
    return { league: lg, id: String(g.gamePk), state, detail, away: side("away"), home: side("home"), date: g.gameDate };
  };
  let games = null;        // [{league, id, state, detail, away:{...}, home:{...}}]
  let gamesAt = 0;
  const teamById = (lg, id) => TEAMS.find((t) => t[0] === lg && t[1] === id);
  const easternDate = () => {
    // ESPN's dates parameter is US Eastern
    const p = new Intl.DateTimeFormat("en-US", { timeZone: "America/New_York", year: "numeric", month: "2-digit", day: "2-digit" }).formatToParts(new Date());
    const g = (t) => p.find((x) => x.type === t).value;
    return g("year") + g("month") + g("day");
  };
  const loadGames = async (force) => {
    if (!force && games && Date.now() - gamesAt < 60000) return games;
    const d = easternDate();
    // Golf's and racing's scoreboards have no home and away (golf's is
    // hundreds of kilobytes): not On now.
    const urls = Object.keys(LEAGUES).filter((lg) => !isBoard(lg))
      .map((lg) => [lg, isMlb(lg) ? MLB_DAY + d.slice(0, 4) + "-" + d.slice(4, 6) + "-" + d.slice(6)
        : `${ESPN}${LEAGUES[lg].path}/scoreboard?${LEAGUES[lg].scan}dates=${d}`]);
    const out = [];
    await Promise.all(urls.map(async ([lg, u]) => {
      try {
        const j = await (await fetch(u)).json();
        if (isMlb(lg)) {
          for (const day of j.dates || []) for (const g of day.games || []) out.push(mlbGame(lg, g));
          return;
        }
        for (const ev of j.events || []) {
          const c = ev.competitions?.[0];
          if (!c) continue;
          const side = (ha) => {
            const x = c.competitors.find((k) => k.homeAway === ha) || {};
            // curatedRank: a college team's rank, 99 when unranked
            const rank = Number(x.curatedRank?.current);
            return { id: Number(x.team?.id), abbr: x.team?.abbreviation || "", score: x.score || "0", name: x.team?.shortDisplayName || x.team?.displayName || "",
              rank: rank >= 1 && rank < 99 ? rank : 0 };
          };
          const type = ev.status?.type || {};
          // "off": ended without being completed, so postponed, canceled or suspended
          const state = type.state === "post" && type.completed === false ? "off" : type.state || "pre";
          out.push({ league: lg, id: ev.id, state, detail: type.shortDetail || "", away: side("away"), home: side("home"), date: ev.date });
        }
      } catch (_) { /* one league failing should not hide the other */ }
    }));
    const rank = { in: 0, pre: 1, post: 2, off: 3 };
    out.sort((a, b) => (rank[a.state] - rank[b.state]) || (a.date < b.date ? -1 : 1));
    games = out;
    gamesAt = Date.now();
    return out;
  };

  const renderGames = async () => {
    const tiles = $("#tiles");
    tiles.className = "games";
    tiles.innerHTML = "";
    tiles.appendChild(el("div", "none", "Loading today's games"));
    const list = await loadGames(false);
    if (league !== "now") return;  // the user moved on while we were loading
    tiles.innerHTML = "";
    const q = $("#q").value.trim().toLowerCase();
    const shown = list.filter((g) => !q || (g.away.name + " " + g.home.name + " " + g.away.abbr + " " + g.home.abbr).toLowerCase().includes(q));
    if (!shown.length) {
      tiles.appendChild(el("div", "none", list.length ? "No game matches" : "No games today"));
      return;
    }
    const cur = S ? refKey(S.team) : "";
    let lastState = null;
    for (const g of shown) {
      if (g.state !== lastState) {
        lastState = g.state;
        const heading = { in: "Live now", pre: "Later today", post: "Final", off: "Postponed or canceled" };
        tiles.appendChild(el("div", "gh", heading[g.state] || "Final"));
      }
      const card = el("div", "game" + (g.state === "in" ? " live" : ""));
      const mkSide = (s) => {
        const t = teamById(g.league, s.id);
        const b = el("button", "side" + (t && keyOf(t) === cur ? " on" : ""));
        b.disabled = !t;
        b.title = t ? "Follow the " + t[3] : "Not in the team list";
        const img = el("img");
        img.loading = "lazy";
        img.src = logoUrl(g.league, s.id, s.abbr);
        img.alt = "";
        b.appendChild(img);
        const txt = el("div", "st");
        txt.appendChild(el("div", "n", s.name));
        txt.appendChild(el("div", "a", ranked(s.rank, s.abbr)));
        b.appendChild(txt);
        b.appendChild(el("div", "sc", g.state === "pre" || g.state === "off" ? "" : String(s.score)));
        b.onclick = () => {
          if (!t) return;
          if (pickFav || pickBet || pickPlayer) return choose(t);
          setGD({ team: keyOf(t) });
          toast("Now following the " + t[3]);
          closePicker();
        };
        return b;
      };
      card.appendChild(mkSide(g.away));
      const mid = el("div", "gm");
      mid.appendChild(el("div", "at", "@"));
      mid.appendChild(el("div", "det", g.detail));
      card.appendChild(mid);
      card.appendChild(mkSide(g.home));
      tiles.appendChild(card);
    }
  };

  // ---- bets ----------------------------------------------------------------------
  // Typed in by hand: the panel never connects to a sportsbook. The firmware
  // ties each bet to a game it reads, grades it at the final and drops it at
  // midnight (components/gameday/bets.h). A new bet posts slot 0, which the
  // firmware puts in its first empty slot, so two pages adding at once never
  // pick the same one (bet=0,<league>,<team id>,<type>,<line>); betdel=<slot>
  // removes one.
  let betTeam = "";      // the add form's team, "nfl:2"
  let betSign = "";      // the spread's sign from the minus and plus buttons, "" until one is tapped
  let betDrawn = null;   // the list as last drawn
  let betPending = [];   // bets posted that the panel doesn't list yet: { l, id, type, half, at }
  const sameBet = (a, b) => a.l === b.l && a.id === b.id && a.type === b.type && a.half === b.half;
  const BET_WORDS = { covering: "COVERING", not_covering: "NOT COVERING", level: "PUSH", won: "WON", lost: "LOST", push: "PUSH", void: "VOID" };
  const betTone = (st) => (st === "covering" || st === "won" ? "good" : st === "not_covering" || st === "lost" ? "bad" : BET_WORDS[st] ? "even" : "");
  const betPoints = (half, sign) => (sign && half > 0 ? "+" : half < 0 ? "-" : "") + Math.floor(Math.abs(half) / 2) + (Math.abs(half) % 2 ? ".5" : "");
  // The line as typed, in half points, or what's wrong with it.
  // A comma works as the decimal point ("3,5"), as a phone set to a comma
  // locale types it.
  const betHalf = (raw, spread) => {
    const v = raw.replace(/\u2212/g, "-").replace(/\s+/g, "").replace(/^([+-]?\d+),(\d+)$/, "$1.$2");
    if (spread && /^(pk|pick|pickem|pick'em)$/i.test(v)) return 0;
    if (!v) return spread ? "Type the spread, like 3.5." : "Type the total, like 47.5.";
    if (!spread && /^[+-]/.test(v)) return "A total has no + or -. Type it like 47.5.";
    const m = /^([+-]?)(\d+)(?:\.(\d+))?$/.exec(v);
    if (!m) return "That isn't a number. Type it like " + (spread ? "3.5." : "47.5.");
    const frac = (m[3] || "").replace(/0+$/, "");
    if (frac && frac !== "5") return "Lines go in whole or half points, like 3 or 3.5.";
    const half = (Number(m[2]) * 2 + (frac ? 1 : 0)) * (m[1] === "-" ? -1 : 1);
    if (spread && Math.abs(half) > 199) return "The panel takes spreads up to 99.5.";
    if (!spread && !half) return "A total is more than 0.";
    if (half > 1999) return "The panel takes totals up to 999.5.";
    return half;
  };
  // The bet's game and where it stands, under its name.
  const betGame = (b) => {
    if (!b.ev) return "Starts when the panel shows a " + b.abbr + " game";
    const vs = b.abbr + " vs " + (b.oa || "?") + " \u00b7 ";
    const score = b.ts + "-" + b.os;
    if (b.st === "pre") return vs + (b.kick * 1000 > Date.now() ? kickLabel(b.kick) : "Waiting for the start");
    if (b.st === "unread") return vs + "Started, not on the panel right now";
    if (b.st === "void") return vs + "Postponed or canceled";
    if (b.st === "won" || b.st === "lost" || b.st === "push") return vs + "Final " + score;
    return vs + score;
  };
  const betChip = (b, text) => el("span", "bet " + betTone(b.st), text);
  const renderBets = () => {
    const card = $("#betCard");
    // Firmware from before bets sends none.
    card.hidden = !S || S.bets === undefined;
    const list = (S && S.bets) || [];
    // The board: the bets on its game.
    const on = game && game.id && game.s !== "NOT_FOUND" ? list.filter((b) => b.ev === String(game.id)) : [];
    const line = $("#betline");
    line.textContent = "";
    for (const b of on) line.appendChild(betChip(b, b.text + (BET_WORDS[b.st] ? " " + BET_WORDS[b.st] : "")));
    line.hidden = !on.length;
    if (card.hidden) return;
    $("#betSum").textContent = list.length ? list.length + " of " + (S.betmax || 8) : "";
    const sig = JSON.stringify(list);
    if (sig === betDrawn) return;
    betDrawn = sig;
    const host = $("#betList");
    const focused = host.contains(document.activeElement);
    host.textContent = "";
    for (const b of list) {
      const row = el("div", "ctl betrow");
      const img = el("img");
      img.alt = "";
      img.loading = "lazy";
      const src = logoUrl(b.l, b.id, b.abbr);
      if (src) img.src = src;
      else img.style.visibility = "hidden";
      row.appendChild(img);
      const name = el("label", null, b.text);
      name.appendChild(el("small", null, betGame(b)));
      row.appendChild(name);
      if (BET_WORDS[b.st]) row.appendChild(betChip(b, BET_WORDS[b.st]));
      const x = el("button", "betdel", "\u2715");
      x.title = "Remove " + b.text;
      x.setAttribute("aria-label", x.title);
      x.onclick = () => setGD({ betdel: b.slot }).then((ok) => ok && toast("Removed " + b.text));
      row.appendChild(x);
      host.appendChild(row);
    }
    if (!list.length) host.appendChild(el("p", "hint", "No bets yet."));
    if (focused && !host.contains(document.activeElement)) $("#betTeam").focus();
  };
  // The sign typed at the start of the line, "" for none.
  const typedSign = (v) => {
    const m = /^\s*([+\-\u2212])/.exec(v);
    return m ? (m[1] === "+" ? "+" : "-") : "";
  };
  const signButtons = Array.from(document.querySelectorAll("#betSign button"));
  const renderBetForm = () => {
    const t = teamByKey(betTeam);
    $("#betTeam").textContent = t ? t[3] : "Pick a team";
    const type = $("#betType").value;
    $("#betLineRow").hidden = type === "ml";
    $("#betSign").hidden = type !== "spread";
    signButtons.forEach((b) => setOn(b, b.dataset.sign === betSign, "aria-pressed"));
    $("#betLineHint").textContent = type === "spread" ? "Tap \u2212 for a favorite or + for an underdog, then the points, like 3.5. 0 is a pick'em"
      : "Total points in the game, like 47.5";
    $("#betLine").placeholder = type === "spread" ? "3.5" : "47.5";
  };
  const betSay = (t) => {
    $("#betMsg").textContent = t;
    $("#betMsg").hidden = !t;
  };
  $("#betTeam").onclick = () => openPicker(false, true);
  $("#betType").onchange = () => { betSay(""); renderBetForm(); };
  $("#betLine").onkeydown = (ev) => { if (ev.key === "Enter") $("#betAdd").click(); };
  // A sign typed in the line lights its button; a button tapped with a sign
  // already typed changes that sign.
  $("#betLine").oninput = () => {
    const s = typedSign($("#betLine").value);
    if (s && s !== betSign) { betSign = s; renderBetForm(); }
  };
  signButtons.forEach((b) => {
    b.onclick = () => {
      betSign = b.dataset.sign;
      const f = $("#betLine");
      if (typedSign(f.value)) f.value = betSign + f.value.replace(/^\s*[+\-\u2212]\s*/, "");
      betSay("");
      renderBetForm();
      f.focus();
    };
  });
  $("#betAdd").onclick = () => {
    if (!S) return;
    const t = teamByKey(betTeam);
    if (!t) return betSay("Pick the team first.");
    const type = $("#betType").value;
    let raw = $("#betLine").value;
    if (type === "spread" && !typedSign(raw) && betSign && /^\s*\d/.test(raw)) raw = betSign + raw.trim();
    const half = type === "ml" ? 0 : betHalf(raw, type === "spread");
    if (typeof half === "string") {
      betSay(half);
      $("#betLine").focus();
      return;
    }
    // An unsigned spread is never taken as one side: the owner says which.
    if (type === "spread" && half !== 0 && !typedSign(raw)) return betSay("Tap \u2212 if the team is the favorite or + if it's the underdog.");
    const list = S.bets || [];
    // A bet posted a moment ago counts too, until the panel lists it: a
    // double tap doesn't add it twice.
    betPending = betPending.filter((p) => Date.now() - p.at < 5000 && !list.some((b) => sameBet(b, p)));
    const want = { l: t[0], id: t[1], type, half };
    if (list.concat(betPending).some((b) => sameBet(b, want))) return betSay("You have that bet already.");
    const max = S.betmax || 8;
    if (list.length + betPending.length >= max) return betSay("All " + max + " bets are in use. Remove one first.");
    betSay("");
    const mine = { ...want, at: Date.now() };
    betPending.push(mine);
    const value = type === "ml" ? "" : betPoints(half, type === "spread");
    setGD({ bet: [0, t[0], t[1], type, value].join(",") }).then((ok) => {
      if (!ok) betPending = betPending.filter((p) => p !== mine);
      else toast("Added " + (type === "ml" ? t[2] + " ML" : type === "spread" ? t[2] + " " + (half ? value : "PK") : type.toUpperCase() + " " + value));
    });
    $("#betLine").value = "";
    betSign = "";
    renderBetForm();
  };
  renderBetForm();

  // ---- favorite players -------------------------------------------------------------
  // Picked from a team's roster, which the browser reads from ESPN (the panel
  // never reads rosters). player=<league>,<athlete id>,<team id>,<name> adds
  // one, or gives one already there a new name; playerdel=<league>,<athlete id>
  // removes one (components/gameday/players.h). The name is what the panel
  // shows: the last name in capitals, with the first initial when a teammate
  // shares it, in at most 10 characters.
  let playerTeam = "";   // the add form's team, "nfl:2"
  let roster = [];       // that team's players: [{id, name, pos, jersey, panel}]
  let rosterFor = "";    // the team the roster read was for
  let playerDrawn = null;
  // Players posted that the panel doesn't list yet: {l, id, name, at, ack},
  // ack when the panel answered the post.
  let playerPending = [];
  const PANEL_NAME_MAX = 10;
  // A letter folded to plain ASCII the way the firmware folds ESPN's words
  // (sport_soccer.cpp's table): \u00d8 is O, \u00c6 AE, \u2019 an apostrophe.
  const foldChar = (ch) => {
    const cp = ch.codePointAt(0);
    if (FOLD2[cp] !== undefined) return FOLD2[cp];
    return cp >= 0xc0 && cp < 0x180 ? FOLD[cp - 0xc0] : ch;
  };
  // The letters the panel takes: capitals, digits, spaces and . ' -, folded
  // as the firmware folds them, and any other accent dropped.
  const panelLetters = (v) => Array.from(String(v || "").normalize("NFC"), foldChar).join("")
    .normalize("NFD").replace(/[\u0300-\u036f]/g, "").toUpperCase()
    .replace(/[^A-Z0-9 .'-]/g, "").replace(/\s+/g, " ").trim();
  const fitName = (v) => {
    let n = panelLetters(v);
    if (n.length > PANEL_NAME_MAX) n = n.replace(/ (JR\.?|SR\.?|II|III|IV|V)$/, "");
    if (n.length > PANEL_NAME_MAX) n = n.slice(0, PANEL_NAME_MAX).replace(/[ .'-]+$/, "");
    return n;
  };
  // ESPN's roster comes grouped by position ({athletes: [{position, items: [...]}]},
  // football and baseball) or flat ({athletes: [...]}, the other sports).
  const rosterAthletes = (j) => {
    const out = [];
    for (const a of (j && Array.isArray(j.athletes) ? j.athletes : [])) {
      if (a && Array.isArray(a.items)) out.push(...a.items);
      else if (a && a.id !== undefined) out.push(a);
    }
    return out.filter((a) => a && /^[0-9]{1,10}$/.test(String(a.id)) && Number(a.id) > 0 && Number(a.id) <= 4294967295);
  };
  const words = (v) => String(v || "").trim().split(/\s+/).filter(Boolean);
  const lastOf = (a) => a.lastName || words(a.displayName || a.fullName).slice(-1)[0] || "";
  const firstOf = (a) => a.firstName || words(a.displayName || a.fullName)[0] || "";
  // Each player's name on the panel, in the roster's order: the last name,
  // with the first initial when a teammate has the same last name, and the
  // first name's first two or three letters when the initial is the same
  // too ("J. HUGHES" and "L. HUGHES", "JE. SMITH" and "JA. SMITH"). Teammates
  // with the same first name keep the initial: nothing in a name tells them apart.
  const panelNames = (list) => {
    const lasts = list.map((a) => panelLetters(lastOf(a)));
    const firsts = list.map((a) => panelLetters(firstOf(a)).replace(/[^A-Z0-9]/g, ""));
    return list.map((a, i) => {
      const mates = firsts.filter((f, j) => j !== i && lasts[j] === lasts[i]);
      if (!mates.length || !firsts[i]) return fitName(lastOf(a));
      const shared = (k) => mates.some((f) => f.startsWith(firsts[i].slice(0, k)));
      let k = 1;
      while (k < 3 && k < firsts[i].length && shared(k)) k++;
      return fitName(firsts[i].slice(0, shared(k) ? 1 : k) + ". " + lastOf(a));
    });
  };
  const playerSay = (t) => {
    $("#playerMsg").textContent = t;
    $("#playerMsg").hidden = !t;
  };
  const loadRoster = async (key) => {
    const t = teamByKey(key);
    if (!t) return;
    rosterFor = key;
    roster = [];
    renderPlayerForm();
    playerSay("Reading the " + t[3] + " roster from ESPN\u2026");
    let list = null;
    try {
      const r = await fetch(`${ESPN}${lgInfo(t[0]).path}/teams/${t[1]}/roster`);
      if (!r.ok) throw new Error("HTTP " + r.status);
      list = rosterAthletes(await r.json());
    } catch (_) {
      list = null;
    }
    if (rosterFor !== key) return;  // another team was picked meanwhile
    if (!list || !list.length) {
      playerSay(list ? "ESPN lists no players for the " + t[3] + " right now."
        : "Couldn't read the " + t[3] + " roster from ESPN. Check this device's internet connection and try again.");
      renderPlayerForm();
      return;
    }
    const names = panelNames(list);
    roster = list.map((a, i) => ({ id: Number(a.id), name: a.displayName || a.fullName || names[i], last: lastOf(a),
      pos: (a.position && a.position.abbreviation) || "", jersey: a.jersey || "", panel: names[i] }))
      .filter((a) => a.panel)
      .sort((a, b) => a.last.localeCompare(b.last) || a.name.localeCompare(b.name));
    playerSay("");
    renderPlayerForm();
  };
  const renderPlayerForm = () => {
    const t = teamByKey(playerTeam);
    $("#playerTeam").textContent = t ? t[3] : "Pick a team";
    const sel = $("#playerPick");
    const sig = rosterFor + ":" + roster.length;
    if (sel.dataset.sig !== sig) {
      sel.dataset.sig = sig;
      sel.textContent = "";
      for (const a of roster) {
        const o = el("option", null, a.name + (a.pos ? " \u00b7 " + a.pos : "") + (a.jersey ? " #" + a.jersey : ""));
        o.value = String(a.id);
        sel.appendChild(o);
      }
    }
    $("#playerPickRow").hidden = $("#playerAdd").hidden = !roster.length;
  };
  // A player posted is added once a read lists them. A read that left after
  // the panel answered the post without them means the panel didn't keep
  // them: every slot was in use (another page may have filled the last one).
  const settlePlayers = () => {
    const list = S.players || [];
    const max = S.playermax || 8;
    playerPending = playerPending.filter((p) => {
      if (list.some((pl) => pl.l === p.l && pl.id === p.id)) {
        toast("Added " + p.name);
        return false;
      }
      if (p.ack && stateLeft > p.ack + APPLY_MS) {
        playerSay(list.length >= max ? "All " + max + " players are in use. Remove one first."
          : "The panel didn't add " + p.name + ". Try again.");
        return false;
      }
      return Date.now() - p.at < 10000;  // no read since: the panel is away
    });
  };
  // The board's line of stat lines, and the card's list.
  const renderPlayers = () => {
    const card = $("#playerCard");
    // Firmware from before favorite players sends none.
    card.hidden = !S || S.players === undefined;
    const lines = (game && Array.isArray(game.pl) && game.s !== "NOT_FOUND" && game.pl) || [];
    const line = $("#playerline");
    line.textContent = "";
    for (const text of lines) line.appendChild(el("span", "bet", text));
    line.hidden = !lines.length;
    if (card.hidden) return;
    settlePlayers();
    const list = S.players || [];
    const sig = JSON.stringify(list);
    if (sig === playerDrawn) return;
    playerDrawn = sig;
    const host = $("#playerList");
    const focused = host.contains(document.activeElement);
    host.textContent = "";
    for (const pl of list) {
      const row = el("div", "ctl playerrow");
      const t = teamById(pl.l, pl.team);
      const img = el("img");
      img.alt = "";
      img.loading = "lazy";
      const src = t ? logoUrl(t[0], t[1], t[2]) : "";
      if (src) img.src = src;
      else img.style.visibility = "hidden";
      row.appendChild(img);
      const name = el("label", null, pl.name);
      name.appendChild(el("small", null, (t ? t[3] : lgInfo(pl.l).name)));
      row.appendChild(name);
      const x = el("button", "betdel", "\u2715");
      x.title = "Remove " + pl.name;
      x.setAttribute("aria-label", x.title);
      x.onclick = () => setGD({ playerdel: pl.l + "," + pl.id }).then((ok) => ok && toast("Removed " + pl.name));
      row.appendChild(x);
      host.appendChild(row);
    }
    if (!list.length) host.appendChild(el("p", "hint", "No favorite players yet."));
    if (focused && !host.contains(document.activeElement)) $("#playerTeam").focus();
  };
  $("#playerTeam").onclick = () => openPicker(false, false, true);
  $("#playerAdd").onclick = () => {
    if (!S) return;
    const t = teamByKey(playerTeam);
    const a = roster.find((x) => String(x.id) === $("#playerPick").value);
    if (!t || !a) return playerSay("Pick the player first.");
    const list = S.players || [];
    const max = S.playermax || 8;
    const same = (pl) => pl.l === t[0] && pl.id === a.id;
    // A player posted a moment ago counts until the panel lists them: a
    // double tap posts once, and a second player can't take the last slot too.
    if (playerPending.some(same)) return;
    if (list.some(same)) return playerSay(a.name + " is a favorite already.");
    if (list.length + playerPending.length >= max) return playerSay("All " + max + " players are in use. Remove one first.");
    playerSay("");
    const mine = { l: t[0], id: a.id, name: a.name, at: Date.now(), ack: 0 };
    playerPending.push(mine);
    setGD({ player: [t[0], a.id, t[1], a.panel].join(",") }).then((ok) => {
      if (ok) mine.ack = Date.now();
      else playerPending = playerPending.filter((p) => p !== mine);
    });
  };
  renderPlayerForm();

  // ---- team chooser ------------------------------------------------------
  // The picker sets My team, with pickFav adds a favorite, with pickBet
  // picks the team for a new bet, and with pickPlayer the team whose roster
  // the Favorite players card lists.
  let pickFav = false;
  let pickBet = false;
  let pickPlayer = false;
  const choose = (t) => {
    if (pickPlayer) {
      playerTeam = keyOf(t);
      loadRoster(playerTeam);
    } else if (pickBet) {
      betTeam = keyOf(t);
      renderBetForm();
    } else if (pickFav) addFav(t);
    else {
      setGD({ team: keyOf(t) });
      toast("Now following the " + t[3]);
    }
    closePicker();
  };
  let league = "now";
  const renderTiles = () => {
    if (league === "now") return renderGames();
    $("#tiles").className = "tiles";
    const q = $("#q").value.trim().toLowerCase();
    const cur = S ? refKey(S.team) : "";
    const tiles = $("#tiles");
    tiles.innerHTML = "";
    // A tab can hold more than one league (Golf: both tours; Racing: both
    // series). A tour or a series is never a favorite yet, and search finds
    // one by its tab's name too.
    // A minor league team is found by its level too ("double-a").
    const words = (t) => (isBoard(t[0]) || levelOf(t) ? lgInfo(t[0]).tab + " " + levelOf(t) + " " : "") + t[3] + " " + t[2];
    let list = TEAMS.filter((t) => !((pickFav || pickBet || pickPlayer) && isBoard(t[0]))
      && (q ? words(t).toLowerCase().includes(q) : lgInfo(t[0]).tab === lgInfo(league).tab));
    if (!list.length) {
      tiles.appendChild(el("div", "none", "No team matches"));
      return;
    }
    // Minor league teams under a heading per level, Triple-A first, each
    // level in name order.
    const byLevel = !q && list.some(levelOf);
    if (byLevel) list = list.slice().sort((a, b) => (a[4] || 0) - (b[4] || 0));
    let level = null;
    for (const t of list) {
      if (byLevel && levelOf(t) !== level) {
        level = levelOf(t);
        tiles.appendChild(el("div", "gh", level));
      }
      const tile = el("button", "tile" + (keyOf(t) === cur ? " on" : ""));
      const img = el("img");
      img.loading = "lazy";
      const src = logoUrl(t[0], t[1], t[2]);
      if (src) img.src = src;
      else img.style.visibility = "hidden";
      img.alt = "";
      tile.appendChild(img);
      tile.appendChild(el("div", "n", t[3]));
      tile.appendChild(el("div", "a", (levelOf(t) || lgInfo(t[0]).prefix) + " · " + t[2]));
      tile.onclick = () => choose(t);
      tiles.appendChild(tile);
    }
  };
  $("#guardClear").onclick = async () => {
    await doAction("power_ok");
    scheduleState(300);
  };
  $("#pick").onclick = () => openPicker(false);
  $("#favAdd").onclick = () => openPicker(true);
  // A dialog: Tab stays inside it, and closing hands focus back to the
  // button that opened it.
  let pickerFrom = null;
  const openPicker = async (fav, bet = false, player = false) => {
    pickFav = fav;
    pickBet = bet;
    pickPlayer = player;
    pickerFrom = document.activeElement;
    $(".sheet").setAttribute("aria-label", player ? "Pick the player's team" : bet ? "Pick the bet's team" : fav ? "Add a favorite" : "Choose a team");
    const cur = player && playerTeam ? teamByKey(playerTeam) : bet && betTeam ? teamByKey(betTeam) : S ? teamByKey(refKey(S.team)) : null;
    const team = fav || bet || player;
    league = cur && !(team && isBoard(cur[0])) ? tabKey(cur[0]) : "nfl";
    // Golf and racing can't be a favorite yet, take a bet or have favorite
    // players: no Golf or Racing tab while picking one.
    tabButtons.forEach((b) => { b.hidden = team && isBoard(b.dataset.lg); });
    try {
      const list = await loadGames(false);
      if (list.some((g) => g.state === "in")) league = "now";
    } catch (_) {}
    $$tabs(league);
    $("#q").value = "";
    renderTiles();
    $("#modal").classList.add("open");
    setTimeout(() => $("#q").focus(), 50);
  };
  // One tab per league that has teams in this firmware's list, and one for
  // leagues that share a tab (Golf, Racing).
  const tabsShown = new Set();
  for (const lg of Object.keys(LEAGUES)) {
    if (!TEAMS.some((t) => t[0] === lg) || tabsShown.has(LEAGUES[lg].tab)) continue;
    tabsShown.add(LEAGUES[lg].tab);
    const b = el("button", null, LEAGUES[lg].tab);
    b.dataset.lg = lg;
    b.setAttribute("role", "tab");
    b.setAttribute("aria-controls", "tiles");
    $(".tabs").appendChild(b);
  }
  // Only the chosen tab is in the Tab order; the arrow keys move between tabs.
  const tabButtons = Array.from(document.querySelectorAll(".tabs button"));
  const $$tabs = (lg) => tabButtons.forEach((b) => {
    setOn(b, b.dataset.lg === lg, "aria-selected");
    b.tabIndex = b.dataset.lg === lg ? 0 : -1;
  });
  const showTab = (b) => { league = b.dataset.lg; $$tabs(league); $("#q").value = ""; renderTiles(); };
  tabButtons.forEach((b, i) => {
    b.onclick = () => showTab(b);
    b.onkeydown = (ev) => {
      // The tabs on show: Golf and Racing hide while adding a favorite.
      const shown = tabButtons.filter((x) => !x.hidden);
      const n = shown.length;
      const at = shown.indexOf(b);
      const j = { ArrowRight: at + 1, ArrowLeft: at - 1 + n, Home: 0, End: n - 1 }[ev.key];
      if (j === undefined || at < 0) return;
      ev.preventDefault();
      shown[j % n].focus();
      showTab(shown[j % n]);
    };
  });
  $("#q").oninput = renderTiles;
  const closePicker = () => {
    if (!$("#modal").classList.contains("open")) return;
    $("#modal").classList.remove("open");
    if (pickerFrom && pickerFrom.isConnected) pickerFrom.focus();
    pickerFrom = null;
  };
  $("#close").onclick = closePicker;
  $("#modal").onclick = (ev) => { if (ev.target.id === "modal") closePicker(); };
  document.addEventListener("keydown", (ev) => { if (ev.key === "Escape") closePicker(); });
  $("#modal").addEventListener("keydown", (ev) => {
    if (ev.key !== "Tab") return;
    const all = Array.from($(".sheet").querySelectorAll("input, button:not([disabled])")).filter((n) => n.tabIndex >= 0);
    if (!all.length) return;
    const first = all[0], last = all[all.length - 1];
    if (ev.shiftKey && document.activeElement === first) { ev.preventDefault(); last.focus(); }
    else if (!ev.shiftKey && document.activeElement === last) { ev.preventDefault(); first.focus(); }
  });

  const renderCurrentTeam = () => {
    const t = S ? teamByKey(refKey(S.team)) : null;
    const img = $("#curLogo");
    if (!t) { img.style.visibility = "hidden"; $("#curName").textContent = "No team chosen"; return; }
    const src = logoUrl(t[0], t[1], t[2]);
    img.style.visibility = src ? "visible" : "hidden";
    if (src) img.src = src;
    $("#curName").textContent = t[3];
    $("#curLg").textContent = lgInfo(t[0]).name + (levelOf(t) ? ", " + levelOf(t) : "");
  };

  // ---- timezone suggestion from the browser --------------------------------
  // Browsers report an IANA zone; the device's list carries one IANA name per
  // entry plus a few common aliases here. Same-zone cities all map to the
  // same POSIX rule, so a near match is still the right choice.
  const TZ_ALIASES = {
    "America/Detroit": "America/New_York", "America/Toronto": "America/New_York", "America/Indiana/Indianapolis": "America/New_York",
    "America/Kentucky/Louisville": "America/New_York", "America/Montreal": "America/New_York", "America/Nassau": "America/New_York",
    "America/Winnipeg": "America/Chicago", "America/Indiana/Knox": "America/Chicago", "America/Menominee": "America/Chicago",
    "America/North_Dakota/Center": "America/Chicago", "America/Matamoros": "America/Chicago", "America/Ojinaga": "America/Chicago",
    "America/Edmonton": "America/Denver", "America/Boise": "America/Denver", "America/Ciudad_Juarez": "America/Denver",
    "America/Vancouver": "America/Los_Angeles", "America/Tijuana": "America/Los_Angeles",
    "America/Juneau": "America/Anchorage", "America/Sitka": "America/Anchorage", "America/Nome": "America/Anchorage",
    "Europe/Dublin": "Europe/London", "Europe/Paris": "Europe/Berlin", "Europe/Madrid": "Europe/Berlin", "Europe/Rome": "Europe/Berlin",
    "Europe/Amsterdam": "Europe/Berlin", "Europe/Brussels": "Europe/Berlin", "Europe/Vienna": "Europe/Berlin", "Europe/Zurich": "Europe/Berlin",
    "Europe/Stockholm": "Europe/Berlin", "Europe/Oslo": "Europe/Berlin", "Europe/Copenhagen": "Europe/Berlin", "Europe/Warsaw": "Europe/Berlin",
    "Europe/Prague": "Europe/Berlin", "Europe/Budapest": "Europe/Berlin", "Europe/Helsinki": "Europe/Athens", "Europe/Kiev": "Europe/Athens",
    "Europe/Kyiv": "Europe/Athens", "Europe/Bucharest": "Europe/Athens", "Europe/Sofia": "Europe/Athens",
    "Asia/Hong_Kong": "Asia/Shanghai", "Asia/Taipei": "Asia/Shanghai", "Asia/Manila": "Asia/Singapore", "Asia/Kuala_Lumpur": "Asia/Singapore",
    "Australia/Melbourne": "Australia/Sydney", "Australia/Hobart": "Australia/Sydney", "Australia/Canberra": "Australia/Sydney",
    "Australia/Darwin": "Australia/Adelaide",
  };
  const browserZoneName = () => {
    let iana = "";
    try { iana = Intl.DateTimeFormat().resolvedOptions().timeZone || ""; } catch (_) {}
    if (!iana) return null;
    const target = TZ_ALIASES[iana] || iana;
    const hit = TZS.find((z) => z[1] === target);
    return hit ? hit[0] : null;
  };
  // The panel follows the browser's zone unless someone picked one by hand.
  // The Time card is a single line; the dropdown only appears on request.
  let tzApplied = false;
  const tzsel = $("#tzsel");
  TZS.forEach((z, i) => { const o = el("option", null, z[0]); o.value = String(i); tzsel.appendChild(o); });
  tzsel.onchange = () => { setGD({ tzauto: 0, tz: tzsel.value }); $("#tzpick").hidden = true; };
  // The line's parts are made once and only their words change, so a redraw
  // never takes the keyboard focus off the button. A button, not a bare
  // link, so the keyboard and screen readers reach it.
  const tzName = el("b");
  const tzHow = el("span", "muted");
  const tzLink = el("button", "lnk");
  const renderTime = () => {
    if (!S) return;
    if (!tzName.isConnected) $("#tzline").replaceChildren(tzName, tzHow, tzLink);
    const cur = S.tz_name || "";
    const auto = !!S.tz_auto;
    const mine = browserZoneName();
    const mineIdx = mine ? TZS.findIndex((z) => z[0] === mine) : -1;
    const pick = $("#tzpick");
    if (document.activeElement !== tzsel) tzsel.value = String(S.tz);
    tzName.textContent = cur || "Not set";
    if (auto) {
      tzHow.textContent = mine ? " · set from this browser" : " · this browser's zone is not in the list";
      tzLink.textContent = "choose manually";
      tzLink.onclick = () => { pick.hidden = !pick.hidden; };
      if (mine && mine !== cur && mineIdx >= 0 && !tzApplied) {
        tzApplied = true;
        setGD({ tz: mineIdx });
        toast("Timezone set to " + mine + " from this browser");
      }
    } else {
      tzHow.textContent = " · chosen by hand";
      tzLink.textContent = mine ? "follow this browser (" + mine + ")" : "change";
      tzLink.onclick = () => {
        if (mine) { tzApplied = false; setGD({ tzauto: 1 }); pick.hidden = true; }
        else pick.hidden = !pick.hidden;
      };
    }
  };

  // ---- up next + splash (from the state document) -------------------------------
  // tbd: ESPN has no kickoff time yet and "kick" is midnight Eastern of the
  // game's day, so show that day in Eastern time and no clock.
  const kickLabel = (epoch, tbd) => {
    const d = new Date(epoch * 1000);
    const opts = { weekday: "short", month: "short", day: "numeric" };
    if (tbd) return d.toLocaleDateString([], { ...opts, timeZone: "America/New_York" }) + " TBD";
    const day = d.toLocaleDateString([], opts);
    const time = d.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
    return day + " " + time;
  };
  // Favorite teams mode: one row per favorite, both logos, state or kickoff.
  const renderFavNext = (box) => {
    const list = (S.next || []).filter((n) => n.t && n.t.id);
    box.hidden = !list.length;
    const host = $("#upnextList");
    host.innerHTML = "";
    for (const n of list) {
      const row = el("div", "un fav" + (n.slot === S.shown ? " shown" : ""));
      const img = el("img");
      img.loading = "lazy";
      img.alt = "";
      img.src = logoUrl(n.t.l, n.t.id, n.t.abbr);
      row.appendChild(img);
      const txt = el("div", "ut");
      const name = ranked(n.trk, n.t.abbr) + (n.oa ? " vs " + ranked(n.ork, n.oa) : "");
      txt.appendChild(el("div", "n", name));
      let when = n.none ? "No game scheduled" : "Not checked yet";
      if (n.s === "IN") when = n.ts + " - " + n.os + (n.detail ? " · " + n.detail : "");
      else if (n.s === "POST") when = endedAs(n) === "Final" ? "Final " + n.ts + " - " + n.os : endedAs(n);
      else if (n.s === "PRE") when = kickLabel(n.kick, n.tbd) + (n.tv ? " · " + n.tv : "");
      txt.appendChild(el("div", "w", when));
      row.appendChild(txt);
      host.appendChild(row);
    }
  };
  const renderUpNext = () => {
    const box = $("#upnext");
    const mode = S ? S.mode || 0 : -1;
    // Favorite teams with no favorites follows the team, so it lists the
    // team's games like My team.
    if (mode === 4 && (S.favs || []).some(refKey)) { renderFavNext(box); return; }
    if (mode !== 0 && mode !== 4) { box.hidden = true; return; }
    const curId = game && game.s !== "NOT_FOUND" ? String(game.id || "") : "";
    const list = (S.next || []).filter((u) => String(u.id) !== curId).slice(0, 3);
    box.hidden = !list.length;
    const host = $("#upnextList");
    host.innerHTML = "";
    const lg = S.team ? S.team.l : "nfl";
    for (const u of list) {
      const row = el("div", "un");
      const img = el("img");
      img.loading = "lazy";
      img.alt = "";
      img.src = logoUrl(lg, u.oi, u.oa);
      row.appendChild(img);
      const txt = el("div", "ut");
      txt.appendChild(el("div", "n", (u.neutral ? "vs " : u.home ? "vs " : "@ ") + ranked(u.ork, u.on || u.oa)));
      txt.appendChild(el("div", "w", kickLabel(u.kick, u.tbd) + (u.tv ? " · " + u.tv : "")));
      row.appendChild(txt);
      host.appendChild(row);
    }
  };
  let lastSplashKey = "";
  let splashTimer = null;
  const renderSplash = () => {
    if (!S || !S.splash) return;
    // One splash per play: the same text with the same score and last play is
    // a repeat. A turnover or a card changes the play, not the score.
    const key = S.splash + "|" + (game ? game.ts + "-" + game.os + "|" + (game.lp || "") : "");
    if (key === lastSplashKey) return;
    lastSplashKey = key;
    const n = $("#splash");
    n.textContent = S.splash;
    n.style.background = "#" + (S.splash_color && S.splash_color !== "000000" ? S.splash_color : "1d4ed8");
    n.classList.add("on");
    clearTimeout(splashTimer);
    splashTimer = setTimeout(() => n.classList.remove("on"), 3000);
  };

  const renderAll = () => {
    renderBoard();
    renderBets();
    renderPlayers();
    renderUpNext();
    renderSplash();
    renderCurrentTeam();
    renderMode();
    renderSettings();
    renderIdle();
    renderPanels();
    renderTime();
    if (S && S.name) $("#hostname").textContent = S.name + ".local";
    renderData();
    renderHealth();
    renderWifi();
  };

  const renderData = () => {
    const kb = S && S.espn_kb;
    if (kb == null) { $("#espnData").textContent = ""; return; }
    const size = kb < 1000 ? kb + " KB" : (kb / 1000).toFixed(kb < 100000 ? 1 : 0) + " MB";
    const reads = S.espn_reads || 0;
    $("#espnData").textContent = size + " in " + reads + (reads === 1 ? " read" : " reads");
  };

  // The health block (components/gameday/health.h). Firmware before it sends
  // none, and the rows stay hidden.
  const RESTARTS = {
    power: "Power on", restart: "Restart", update: "Update", crash: "Crash", watchdog: "Froze (watchdog)",
    brownout: "Power dip (brownout)", usb: "USB reset",
  };
  const restartName = (k) => RESTARTS[k] || "Other";
  // The Wi-Fi history (components/gameday/wifi_history.h). Firmware before it
  // sends none, and the rows stay hidden.
  const WIFI_CAUSES = {
    signal: "signal lost", router: "dropped by the router", password: "sign-in refused",
    roam: "moved to another access point", other: "other",
  };
  const span = (s) => {
    if (s < 60) return s + " s";
    const m = Math.round(s / 60);
    if (m < 60) return m + " min";
    const h = Math.floor(m / 60);
    if (h < 24) return h + " h" + (m % 60 ? " " + (m % 60) + " min" : "");
    return Math.floor(h / 24) + " d" + (h % 24 ? " " + (h % 24) + " h" : "");
  };
  // An empty health block (the firmware's fallback when it doesn't fit its
  // buffer) counts as none.
  const healthOf = () => (S && S.health && S.health.up != null ? S.health : null);
  const renderHealth = () => {
    const h = healthOf();
    $("#health").hidden = !h;
    if (!h) return;
    $("#hStart").textContent = restartName(h.start) + ", " + span(h.up) + " ago";
    $("#hBoots").textContent = h.boots > 1 ? "Start " + h.boots + " since the power came on" : "";
    $("#hFault").textContent = !h.fault ? "None"
      : restartName(h.fault) + (h.fault_ago != null ? ", " + span(h.fault_ago) + " ago" : "");
    $("#hHeap").textContent = Math.round(h.heap_kb) + " KiB, largest piece " + Math.round(h.block_kb) + " KiB";
    const answer = (code, ago) => (code === 200 ? "OK" : code === 0 ? "No answer" : "HTTP " + code) + ", " + span(ago) + " ago";
    $("#hEspn").textContent = h.espn == null ? "None yet" : answer(h.espn, h.espn_ago);
    // Only once the panel has read minor league scores from MLB
    $("#hMlbRow").hidden = h.mlb == null;
    if (h.mlb != null) $("#hMlb").textContent = answer(h.mlb, h.mlb_ago);
    const chan = { stable: "stable", beta: "beta", bar: "bar beta" }[h.channel];
    // The build time comes in Unix seconds; shown in the browser's own time
    const b = h.built ? new Date(h.built * 1000) : null;
    const builtText = b ? b.toLocaleDateString([], { month: "short", day: "numeric" }) + ", " +
      b.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }) : "";
    $("#hFw").textContent = (h.version || "") + (chan ? " " + chan : "") + (builtText ? ", built " + builtText : "");
    // Live here, so they no longer wait on the RSSI and heap sensors, which
    // are off until someone turns them on in Home Assistant. The Wi-Fi
    // object, when there is one, fills in the signal.
    if (!S.wifi) $("#rssi").textContent = h.rssi != null ? h.rssi + " dBm" : "Not connected";
    $("#psram").textContent = h.psram_kb ? Math.round(h.psram_kb) + " KiB" : "";
    renderGuard(h);
  };

  // Heat and power protection (components/gameday/guard.h). heat is the
  // dimming step, power "capped" after two brownout restarts in a row.
  const HEAT_PERCENT = [100, 70, 45, 25];
  const renderGuard = (h) => {
    const hasGuard = h && h.heat != null;
    $("#hTempRow").hidden = !hasGuard || h.temp == null;
    $("#hPowerRow").hidden = !hasGuard;
    const capped = hasGuard && h.power === "capped";
    const hot = hasGuard && h.heat > 0;
    $("#guard").hidden = !capped && !hot;
    $("#guard").classList.toggle("power", capped);
    if (!hasGuard) return;
    const pct = HEAT_PERCENT[Math.min(h.heat, HEAT_PERCENT.length - 1)];
    if (h.temp != null) $("#hTemp").textContent = Math.round(h.temp) + " C" + (hot ? ", dimmed to " + pct + "%" : "");
    $("#hPower").textContent = (h.brownouts || 0) + (capped ? ", brightness capped" : "");
    $("#guardClear").hidden = !capped;
    if (capped) {
      $("#guardName").textContent = "Check the power supply";
      $("#guardText").textContent = "The panel restarted twice in a row because its power dipped, so its brightness is held down. "
        + "Use the power supply that came with it, not a phone charger or a USB port, then press Clear.";
    } else if (hot) {
      $("#guardName").textContent = "Too warm, dimming";
      $("#guardText").textContent = "The panel is running hot, so it is at " + pct + "% of its brightness until it cools. "
        + "Keep it out of direct sun and give it some air behind it.";
    }
  };
  // The history across the full width (a day at most), newest at the right,
  // gaps where the panel was off Wi-Fi (a red tick along the bottom). -95 to
  // -35 dBm bottom to top.
  const drawSpark = (svg, hist, step) => {
    const W = hist.length, H = 48, NS = "http://www.w3.org/2000/svg";
    const y = (v) => (H - 2 - (Math.min(-35, Math.max(-95, v)) + 95) / 60 * (H - 4)).toFixed(1);
    let d = "", gaps = "", lo = 0, hi = -127, weak = 0, n = 0;
    hist.forEach((v, i) => {
      const x = i;
      if (v == null) { gaps += "M" + x + " " + (H - 3) + "V" + H; return; }
      d += (i > 0 && hist[i - 1] != null ? "L" : "M") + x + " " + y(v);
      if (i === hist.length - 1 || hist[i + 1] == null) d += "h0.01";  // a lone point still shows
      lo = Math.min(lo, v); hi = Math.max(hi, v); n++;
      if (v < -75) weak++;
    });
    const path = (cls, p) => {
      const e = document.createElementNS(NS, "path");
      e.setAttribute("class", cls);
      e.setAttribute("d", p);
      e.setAttribute("vector-effect", "non-scaling-stroke");
      return e;
    };
    svg.setAttribute("viewBox", "0 0 " + (W - 1) + " " + H);
    svg.replaceChildren(path("th", "M0 " + y(-75) + "H" + W));
    if (gaps) svg.appendChild(path("gap", gaps));
    if (d) svg.appendChild(path("ln", d));
    svg.setAttribute("aria-label", n ? "Signal over the last " + span(W * step) + ": " + lo + " to " + hi
      + " dBm" + (weak ? ", weak for " + span(weak * step) : "") : "No signal readings yet");
  };
  const renderWifi = () => {
    const w = S && S.wifi;
    $("#wifiDiag").hidden = !w;
    if (!w) return;
    $("#rssi").textContent = w.rssi != null ? w.rssi + " dBm" : "Not connected";
    $("#wifiWeak").hidden = !w.weak;
    const hist = w.hist || [], step = w.step || 300;
    $("#wifiSparkBox").hidden = hist.length < 2;
    if (hist.length >= 2) {
      drawSpark($("#wifiSpark"), hist, step);
      $("#wifiFrom").textContent = (hist.length * step >= 86400 ? "24 h" : span(hist.length * step)) + " ago";
    }
    // Up less than a day (by the history), every drop since start is in the day.
    const day = hist.length > 0 && hist.length * step < 86400;
    const count = day ? w.drops : w.day;
    $("#wifiDrops").textContent = (count || "None") + (count ? (day ? " since start" : " in 24 h") : "");
    const causes = Object.entries(w.causes || {}).filter(([k]) => k !== "roam");
    let line = "";
    if (w.last) {
      line = "Last: " + (WIFI_CAUSES[w.last] || "other") + ", " + span(w.last_ago) + " ago"
        + (w.last_down != null ? ", back in " + span(w.last_down) : "");
      if (causes.length > 1) line += ". " + causes.map(([k, c]) => c + " " + (WIFI_CAUSES[k] || k)).join(", ");
    }
    $("#wifiLast").textContent = line;
  };

  // After an install the device reboots. The event stream only retries every
  // 30 seconds, so poll the root page instead and reload as soon as it answers.
  let es = null;
  let streamAt = 0;
  const showRestart = (line) => {
    restart = line;
    renderGone();
  };
  // Reboot, the resets and the panel picker are far down the page; the line is on the board.
  const toBoard = () => $("#board").scrollIntoView({ behavior: "smooth", block: "start" });
  const waitForReboot = (fromVersion, line) => {
    const started = Date.now();
    let sawDown = false;
    if (es) { es.close(); es = null; }
    setConn(false);
    showRestart(line);
    // Ask the device which version it runs; reload as soon as that changes.
    // A connection to a rebooting device can hang, so each probe gets 1.5s.
    const tick = async () => {
      const ctl = new AbortController();
      const timer = setTimeout(() => ctl.abort(), 1500);
      try {
        // detail=all carries current_version; the plain state does not
        const r = await fetch("/update/Firmware?detail=all&_=" + Date.now(), { cache: "no-store", signal: ctl.signal });
        if (r.ok) {
          const j = await r.json();
          const changed = j.current_version && fromVersion !== "" && j.current_version !== fromVersion;
          const settled = !String(j.state || "").toUpperCase().includes("INSTALLING");
          // Once the device has been seen down, a settled answer means it is back.
          if (changed || (sawDown && settled)) { location.reload(); return; }
        }
      } catch (_) { sawDown = true; }
      clearTimeout(timer);
      if (Date.now() - started < 5 * 60 * 1000) { setTimeout(tick, 2000); return; }
      // Not back in five minutes: stop saying it's restarting and listen again.
      restart = "";
      renderGone();
      // An install that never restarted the panel didn't happen: the firmware
      // box goes back to what the panel offers.
      if (installing) {
        installing = false;
        if (ents[byName["Firmware"]]) renderUpdate(ents[byName["Firmware"]]);
      }
      connect();
    };
    setTimeout(tick, 3000);
  };
  // Both resets blank the saved network, so the panel comes back as its own
  // setup hotspot and never on this one. Stop asking it.
  const intoSetup = (why) => {
    stopped = true;
    if (es) { es.close(); es = null; }
    clearInterval(pollTimer);
    clearTimeout(stateTimer);
    setConn(false);
    const tail = S && S.name && S.name.includes("-") ? S.name.slice(S.name.lastIndexOf("-") + 1) : "";
    showRestart(why + " To set it up again, join its \"Game Day" + (tail ? " " + tail : "") + "\" Wi-Fi.");
  };

  // ---- check for updates -----------------------------------------------------
  // The device only sends a state event when the result changes, so "no update"
  // after a check would look like nothing happened. Ask for the result directly.
  const checkForUpdates = async (btn) => {
    const uid = byName["Firmware"];
    btn.disabled = true;
    const label = btn.textContent;
    btn.textContent = "Checking";
    $("#fw").hidden = false;
    $("#fwText").textContent = "Checking for updates";
    $("#fwSub").textContent = "";
    $("#fwInstall").hidden = true;
    fwChecking = true;
    await new Promise((r) => setTimeout(r, 4000));
    // A panel that lost power never closes the connection, so give up on it.
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), 8000);
    try {
      // detail=all carries current_version; the plain state does not
      const r = await fetch("/update/Firmware?detail=all&_=" + Date.now(), { cache: "no-store", signal: ctl.signal });
      if (r.ok && uid) {
        const j = await r.json();
        onState(Object.assign({ id: uid }, j));
        const st = String(j.state || "").toUpperCase();
        // The firmware row shows the result; only shout when there is something to
        // do. The device offers any different version, an older one included.
        if (st.includes("AVAILABLE")) { if (isNewer(j.value, j.current_version) !== false) toast("Update available: v" + j.value); }
        else if (st.includes("INSTALLING")) { /* the install is running; nothing to say */ }
        else if (!st.includes("NO UPDATE")) toast("Could not reach the update server. Try again in a minute.");
      } else if (uid) {
        renderUpdate(ents[uid]);
        toast("Could not read the update status");
      }
    } catch (_) {
      if (uid) renderUpdate(ents[uid]);
      toast("Device did not respond");
    }
    clearTimeout(timer);
    fwChecking = false;
    btn.textContent = label;
    btn.disabled = false;
  };

  // State events carry the update state but not current_version; fetch the
  // full detail whenever the state changes so the box shows the running version.
  let fwDetailInflight = false;
  const refreshFirmwareDetail = async (id) => {
    if (fwDetailInflight) return;
    fwDetailInflight = true;
    try {
      const r = await fetch("/update/Firmware?detail=all&_=" + Date.now(), { cache: "no-store" });
      if (r.ok) {
        const j = await r.json();
        ents[id] = Object.assign(ents[id] || {}, j, { id });
        renderUpdate(ents[id]);
      }
    } catch (_) {}
    fwDetailInflight = false;
  };

  // true if a is newer than b, false if older, null if either is unreadable.
  // As in the firmware (auto_update.h), betas ("2026.10.1-beta.812") order by
  // their build number and come before the release they lead up to.
  const isNewer = (a, b) => {
    const parse = (v) => {
      const [base, beta] = String(v || "").replace(/^v/, "").split("-beta.");
      const p = base.split(".").map((n) => parseInt(n, 10));
      const build = beta === undefined ? Infinity : parseInt(beta, 10);
      if (p.length < 3 || p.some(isNaN) || isNaN(build)) return null;
      return [p[0], p[1], p[2], p[3] || 0, build];
    };
    const pa = parse(a), pb = parse(b);
    if (!pa || !pb) return null;
    for (let i = 0; i < 5; i++) if (pa[i] !== pb[i]) return pa[i] > pb[i];
    return false;
  };

  // ---- firmware update -----------------------------------------------------
  // The manifest carries a one-line summary; the release on GitHub has the
  // real bullet list. Fetch that from the browser and fall back to the summary.
  const notesCache = {};
  const bulletsFrom = (text) => String(text || "").split(/\r?\n/).filter((l) => l.startsWith("- ")).map((l) => l.slice(2).trim());
  const renderNotes = (host, tag, summary) => {
    const list = el("ul", "notes");
    host.appendChild(list);
    const fill = (items) => {
      list.innerHTML = "";
      items.forEach((t) => list.appendChild(el("li", null, t)));
    };
    // Summary first so something shows right away; the manifest joins the bullets with " | ".
    const quick = String(summary || "").split(" | ").map((t) => t.trim()).filter(Boolean);
    if (quick.length) fill(quick);
    if (!tag) return;
    if (notesCache[tag]) { fill(notesCache[tag]); return; }
    fetch("https://api.github.com/repos/" + REPO + "/releases/tags/" + encodeURIComponent(tag), { headers: { Accept: "application/vnd.github+json" } })
      .then((r) => (r.ok ? r.json() : null))
      .then((j) => {
        const items = j ? bulletsFrom(j.body) : [];
        if (items.length) { notesCache[tag] = items; fill(items); }
      })
      .catch(() => {});
  };
  // A release has its own section on the site's changelog, in owner words; a
  // beta has none, so it keeps the manifest's link to its commit, and only
  // an https one, since any other kind of address could run script on a tap.
  const notesLink = (u) => {
    if (/^\d+(\.\d+)+$/.test(String(u.value || ""))) return SITE_URL + "/changelog/#v" + u.value;
    const url = String(u.release_url || "");
    return /^https:\/\//i.test(url) ? url : "";
  };
  let installing = false;
  let fwChecking = false;  // Check for updates is waiting for the panel's answer
  // What the firmware box reads from the state document, and what it last drew with.
  const fwInputs = () => JSON.stringify(S ? [S.update_failed || "", !!(S.autoupd || S.bar)] : null);
  let fwDrawn = fwInputs();
  const renderUpdate = (u) => {
    const box = $("#fw");
    box.hidden = false;
    const cur = u.current_version ? "v" + u.current_version : "";
    const latest = u.value ? "v" + u.value : "";
    const st = String(u.state || "").toUpperCase();
    const btn = $("#fwInstall");
    const sub = $("#fwSub");
    btn.hidden = true;
    fwDrawn = fwInputs();
    if (st.includes("INSTALLING") || installing) {
      $("#fwText").textContent = "Installing " + (latest || "update");
      sub.textContent = "Keep the panel powered. It reboots when done and this page reconnects.";
    } else if (st.includes("AVAILABLE") && isNewer(u.value, u.current_version) === false) {
      // The device offers any different version; a lower one is a downgrade.
      // Only the beta feeds carry "-beta.<build>" versions (scripts/beta_version.sh),
      // so the version on offer names the feed it came from, whatever the
      // Follow beta switch says right now.
      const chan = /-beta\.\d+$/.test(String(u.value || "")) ? "beta" : "release";
      $("#fwText").textContent = "You're ahead of the latest " + chan + ", " + cur;
      sub.textContent = "The newest " + chan + " is " + latest + ". Nothing to install.";
    } else if (st.includes("AVAILABLE")) {
      // The panel rolled back from this very version (a crash in its first
      // minute); it won't install it on its own again, but Install still tries.
      const failed = !!(S && S.update_failed && S.update_failed === u.value);
      if (failed) {
        $("#fwText").textContent = "The update to " + latest + " didn't take and the panel went back to " + (cur || "its old version");
        sub.textContent = "Install tries it again." + (S.autoupd || S.bar ? " It won't install this one on its own." : "");
      } else {
        $("#fwText").textContent = "Update available: " + latest;
        sub.textContent = (cur ? "You have " + cur + "." : "") + (S && (S.autoupd || S.bar) ? " It installs on its own between 3 and 5 AM." : "");
      }
      renderNotes(sub, latest, u.summary);
      btn.hidden = false;
      btn.onclick = async () => {
        const ask = failed ? "Try " + latest + " again? It didn't take last time. The panel will reboot." : "Install " + latest + " now? The panel will reboot.";
        if (!confirm(ask)) return;
        installing = true;
        renderUpdate(u);
        // Only an install the panel took is waited out; a failed post has said so.
        if (!(await post(u.id, "install"))) {
          installing = false;
          renderUpdate(u);
          return;
        }
        toast("Installing " + latest);
        waitForReboot(u.current_version, "Installing " + latest + ". This page reloads when the panel is back.");
      };
    } else if (st.includes("NO UPDATE")) {
      $("#fwText").textContent = "You're up to date, " + cur;
      sub.textContent = "";
    } else {
      $("#fwText").textContent = "Firmware " + cur;
      sub.textContent = "Not checked yet. It checks a few seconds after the clock syncs, or tap Check for updates.";
    }
    const notesUrl = notesLink(u);
    if (notesUrl && !st.includes("INSTALLING")) {
      const a = el("a", null, "Release notes");
      a.href = notesUrl;
      a.target = "_blank";
      a.rel = "noopener";
      sub.appendChild(document.createTextNode(sub.textContent ? " " : ""));
      sub.appendChild(a);
    }
  };

  // ---- saved Wi-Fi networks ---------------------------------------------
  // GET /gameday/wifi lists the networks the panel remembers, names only, in
  // the order it tries them. Firmware without the list answers 404 and the
  // section stays hidden.
  const renderNetworks = (j) => {
    const nets = j.networks || [];
    const list = $("#nets");
    list.innerHTML = "";
    nets.forEach((n, i) => {
      const row = el("div", "ctl");
      const label = el("label", null, n.ssid);
      if (n.connected) label.appendChild(el("small", null, "Connected"));
      else if (i === 0) label.appendChild(el("small", null, "Tried first"));
      row.appendChild(label);
      const b = el("button", "btn danger", "Forget");
      b.onclick = async () => {
        if (!confirm("Forget " + n.ssid + "?")) return;
        b.disabled = true;
        try {
          const r = await fetch("/gameday/wifi?forget=" + encodeURIComponent(n.ssid), { method: "POST" });
          // A forgotten network the panel is on stays connected only until
          // that connection drops or the panel restarts; the last saved one
          // stays in ESPHome's own slot, so the panel joins it again.
          const after = !n.connected ? ""
            : nets.length === 1 ? "; the panel keeps using it and saves it again when it reconnects"
            : "; it stays connected until this connection drops or the panel restarts";
          toast(r.ok ? "Forgot " + n.ssid + after : "Could not forget " + n.ssid);
        } catch (_) {
          toast("Could not forget " + n.ssid);
        }
        loadNetworks();
      };
      row.appendChild(b);
      list.appendChild(row);
    });
    $("#netsEmpty").hidden = nets.length > 0;
    $("#netsCard").hidden = false;
  };
  const loadNetworks = async () => {
    try {
      const r = await fetch("/gameday/wifi?_=" + Date.now(), { cache: "no-store" });
      if (!r.ok) { $("#netsCard").hidden = true; return; }
      renderNetworks(await r.json());
    } catch (_) {
      $("#netsCard").hidden = true;
    }
  };

  // ---- event stream ------------------------------------------------------
  const onState = (e) => {
    ents[e.id] = Object.assign(ents[e.id] || {}, e);
    if (e.name) byName[e.name] = e.id;
    const name = ents[e.id].name;
    const def = controlDefs.find((d) => d.name === name);
    if (def) {
      if (!built[e.id]) built[e.id] = buildControl(def, ents[e.id]);
      built[e.id].update(ents[e.id]);
    }
    switch (name) {
      // Sent on every board update: re-read the state document. Its value is
      // Home Assistant's shorter line, so the ticker and last play come from there.
      case "Game Status": scheduleState(150); break;
      case "Firmware": renderUpdate(ents[e.id]); refreshFirmwareDetail(e.id); break;
      case "WizMote Status": $("#wizStatus").textContent = e.value || ""; break;
      case "IP": $("#ip").textContent = e.value || ""; break;
      case "RSSI": if (!(S && (S.wifi || healthOf()))) $("#rssi").textContent = e.value ? e.value + " dBm" : ""; break;
      case "Free Heap (PSRAM)": if (!healthOf()) $("#psram").textContent = e.value ? Math.round(e.value) + " KiB" : ""; break;
    }
  };

  const connect = () => {
    if (es) es.close();
    streamAt = Date.now();
    es = new EventSource("/events");
    es.addEventListener("ping", (ev) => {
      setConn(true);
      answered();
      if (!ev.data) return;
      try {
        const p = JSON.parse(ev.data);
        if (p.title) $("#devname").textContent = p.title;
        if (p.comment) $("#ver").textContent = p.comment;
      } catch (_) {}
    });
    es.addEventListener("state", (ev) => {
      try { onState(JSON.parse(ev.data)); } catch (_) {}
    });
    es.onopen = () => { setConn(true); answered(); fetchState(); };
    // A dropped stream is often only a phone waking up; ask the panel before
    // saying it's gone.
    es.onerror = () => { setConn(false); fetchState(); };
  };
  connect();
  fetchState();
  loadNetworks();
  // Fallback poll in case an event is missed; the ticker event is the fast path.
  const pollTimer = setInterval(fetchState, 15000);
})();
