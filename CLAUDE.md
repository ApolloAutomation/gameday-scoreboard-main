# Game Day Scoreboard firmware

ESPHome firmware for a standalone sports scoreboard (NFL, college football,
MLB, NBA, WNBA, men's college basketball, NHL, MLS and the Premier League) on
an ESP32-S3 HUB75 controller. Apollo Automation owns and maintains it, at
github.com/ApolloAutomation/gameday-scoreboard-main; the product name is
"Game Day Scoreboard". One top-level YAML per pinout: gameday.yaml
(MoonHub75) and gameday-scoreboard75.yaml (the scoreboard75 controller
board); each picks its pin map from firmware/controllers/ and shares
everything else via gameday-common.yaml.
AGENTS.md has the layout, build and test commands shared with outside
contributors; read it too.

## Hard rules

- Commit with your own git identity. Multiline commit messages via
  `git commit -F <file>`. A commit message is one plain sentence about the
  behavior that changed.
- This repo is PUBLIC and open source. Never commit handoffs, session
  notes, specs, plans or reviews here: only product code, CHANGELOG, README,
  the docs and the site. ARCHITECTURE.md and REVIEW_REPORT.md at the root
  are public documents and are fine. Maintainer notes are kept privately and
  loaded through a gitignored CLAUDE.local.md. CI's "No private notes" job
  fails on the usual notes files.
- The docs site (docs/wiki, Astro + Starlight) is part of every change. A PR
  that changes what an owner or DIY builder sees or does updates or adds
  the page for it under docs/wiki/src/content/docs/ in the same PR; CI
  ("Docs updated") fails a PR into `dev` that touches firmware/ or
  components/ without a docs change unless it carries the label
  `docs: not needed`. Behavior that `main` doesn't have yet goes in a
  `:::note[Coming in the next release]` aside, removed at release time.
  Docs describe the firmware on the branch they sit on, written for the
  owner, no internal names, cards or notes. `cd docs/wiki && npm run
  build` must pass.
- PR descriptions say what changed and how it was tested, nothing else: no
  links to private repos or trackers, no ticket numbers from them, no
  private notes. Reviews and progress notes stay private.
- Every parallel agent works in its own git worktree.
- Branch from `dev` and open PRs against `dev` (the default branch), never
  `beta` or `main`. Changes move `dev` to `beta` to `main` by promotion PRs
  only: `beta` only takes a PR from `dev`, and `main`, which is what
  owners run and what the live site deploys from, only takes a PR from
  `beta`. Release tags go on `main`. CI fails a PR into `beta` or `main`
  from any other branch and refuses to publish a tag that isn't on `main`.
- ESPHome versions by channel: `dev` and `beta` may pin an ESPHome beta;
  `main` and release tags build only on a final release. The pin is
  `ESPHOME_VERSION` at the top of build.yml, and CI (Branch rules) fails a
  PR into `main` or a `v*` tag when it isn't a final like `2026.10.0`. A fix
  that is only in a beta: copy the component into `components/` and stay
  on the final, wait for the final or ask for a patch-release backport, or
  an Apollo maintainer's `esphome-beta-ok` label on the promotion PR (then
  re-run the job). Diagram in CONTRIBUTING.md.
- Apollo Automation decides the panel's network posture: OTA with no
  password, `api:` with no key and the device page open ("the panel trusts
  the home network"). Don't change it in a feature PR. Prioritize
  owner-visible failures.
- Bump `version` in firmware/gameday-common.yaml and add a CHANGELOG.md
  section for every release; a tag without a matching section fails CI.
  Feature PRs leave both alone.
- Use `esphome compile`, not `esphome config`, to catch C++ errors. Host
  tests: `make -C tests`. Rebuild the web bundle with
  `python3 scripts/build_web.py` after editing firmware/web/app.js, app.css
  or demo.js, and commit bundle.js and bundle-demo.js with it.
- The size budget in firmware/budget.json is a ceiling. A change that needs
  more raises that one entry in the same PR and says why.
- Never hand-edit components/gameday/teams.h, team_colors.h or
  timezones_parsed.h: rerun their generator scripts.
- Never commit secrets.yaml, Wi-Fi details or league and team logos.
- The site address (the GitHub Pages address today) lives in a few known
  places; CONTRIBUTING.md's "Moving to a new address" lists them.
