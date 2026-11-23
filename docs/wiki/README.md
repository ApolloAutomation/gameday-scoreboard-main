# Apollo Pulse docs

This is the documentation site for Apollo Pulse, formerly Game Day Scoreboard, built with [Astro](https://astro.build) and [Starlight](https://starlight.astro.build). It covers setting up a panel, using it, fixing problems, and building one on your own board. It's published with the project's site at https://apolloautomation.github.io/gameday-scoreboard-main/docs/. You can read the pages on GitHub under `src/content/docs/`, but links between pages and the note and caution boxes only work in the built site, so run it locally (below) to browse it properly.

## Run it locally

You need Node 24 or newer.

```
cd docs/wiki
npm install
npm run dev
```

`npm run dev` serves the site at http://localhost:4321 and reloads as you edit. `npm run build` makes the static site in `dist/` along with a search index, and `npm run preview` serves that build.

## Where pages live

Pages are Markdown or MDX files under `src/content/docs/`, one folder per sidebar group:

- `get-started/` for unboxing, install and first setup
- `using/` for everyday use and settings
- `help/` for troubleshooting
- `diy/` for running the firmware on your own board

Each folder has an `index.md` for its landing page. The sidebar builds itself from the folders, so a new file shows up without touching the config.

Every page starts with frontmatter:

```
---
title: Pick your team
description: One sentence that says what the page covers.
sidebar:
  order: 3
---
```

`title` is required and `description` should always be there. `sidebar.order` sets the position within its group, lowest first. For callouts use Starlight's asides, `:::note`, `:::tip` and `:::caution`, each closed with `:::`.

Images go in `src/assets/` and are pulled in with a relative Markdown image link. Files in `public/` are served as they are.

## The changelog page

`src/content/docs/changelog.md` is generated from the `CHANGELOG.md` at the repo root by `scripts/changelog.mjs`, which runs before every build. Do not edit the generated file and do not commit it (it is gitignored). Change `CHANGELOG.md` instead.

## Publishing

The docs go on the project's site under `/docs/`. The site deploys (`.github/workflows/site.yml`, and the release deploy in `build.yml`) build them from `main` or the release tag and copy `dist/` into the site, so the published docs are `main`'s. `astro.config.mjs` takes the site's address from `SITE_URL` (the GitHub Pages address when it's unset) and builds for its `/docs/` path. Write links between pages from the docs' root, like `/using/weather/`: the build adds the path in front. A pull request that touches `docs/wiki/` or `CHANGELOG.md` builds the site through the Docs workflow (`.github/workflows/docs.yml`) to catch broken pages and links.

A pull request into `dev` that changes `firmware/` or `components/` must also change a page under `src/content/docs/`, or carry the label `docs: not needed`. The Docs updated workflow (`.github/workflows/docs-check.yml`) enforces that.
