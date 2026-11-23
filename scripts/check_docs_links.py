#!/usr/bin/env python3
"""Every on-site link in the built docs reaches a page and anchor.

Images, scripts and stylesheets have to be there too. --base is the path
the folder is served at (the docs' base, /gameday-scoreboard-main/docs for
docs/wiki/dist), so links written with it resolve inside the folder.

    python3 scripts/check_docs_links.py --base /gameday-scoreboard-main/docs docs/wiki/dist
    python3 scripts/check_docs_links.py --selftest
"""

import sys
import tempfile
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urljoin, urlsplit


class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids, self.links, self.assets = set(), [], []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        for key in ("id", "name") if tag == "a" else ("id",):
            if a.get(key):
                self.ids.add(a[key])
        if tag == "a" and a.get("href") is not None:
            self.links.append(a["href"])
        elif tag in ("img", "script") and a.get("src"):
            self.assets.append(a["src"])
        elif tag == "link" and a.get("href") and a.get("rel") in ("stylesheet", "icon"):
            self.assets.append(a["href"])


def url_of(dist: Path, page: Path, base: str = "") -> str:
    rel = page.relative_to(dist).as_posix()
    return base + "/" + (rel[: -len("index.html")] if rel.endswith("index.html") else rel)


def target(dist: Path, path: str, base: str = ""):
    """The file a site path serves, or None."""
    if base:
        if path != base and not path.startswith(base + "/"):
            return None
        path = path[len(base):]
    rel = unquote(path).lstrip("/")
    for cand in (dist / rel, dist / rel / "index.html", dist / (rel.rstrip("/") + ".html")):
        if cand.is_file():
            return cand
    return None


def check(dist: Path, base: str = "") -> list:
    base = base.rstrip("/")
    pages, errors = {}, []
    for f in sorted(dist.rglob("*.html")):
        p = Page()
        p.feed(f.read_text(encoding="utf-8", errors="replace"))
        pages[f] = p
    for f, p in pages.items():
        here = url_of(dist, f, base)
        for href in p.links + p.assets:
            parts = urlsplit(href)
            if parts.scheme or parts.netloc or href.startswith(("mailto:", "tel:", "javascript:", "data:")):
                continue
            url = urlsplit(urljoin(here, href))
            dest = target(dist, url.path, base)
            where = f"{f.relative_to(dist).as_posix()}: {href}"
            if dest is None and href in p.assets:
                errors.append(f"{where} is a file that isn't there")
                continue
            if dest is None:
                errors.append(f"{where} goes to no page")
                continue
            frag = unquote(url.fragment)
            if frag and dest.suffix == ".html":
                ids = pages[dest].ids if dest in pages else set()
                if frag not in ids:
                    errors.append(f"{where} names #{frag}, which {dest.relative_to(dist).as_posix()} doesn't have")
    return errors


def selftest() -> None:
    with tempfile.TemporaryDirectory() as d:
        dist = Path(d)
        (dist / "using" / "weather").mkdir(parents=True)
        (dist / "using" / "weather" / "index.html").write_text(
            '<h1 id="_top">W</h1><h2 id="radar">Radar</h2><a href="#_top">top</a><a href="../ticker/">t</a>')
        (dist / "index.html").write_text(
            '<a href="/using/weather/#radar">ok</a><a href="/using/weather/#no-such">bad anchor</a>'
            '<a href="/using/nope/">bad page</a><a href="https://example.com/x">off site</a>'
            '<a href="mailto:a@b.c">mail</a><a href="/favicon.svg">icon</a><a href="/using/weather/?x=1">query</a>')
        (dist / "favicon.svg").write_text("<svg/>")
        errors = check(dist)
        assert len(errors) == 3, errors
        assert any("#no-such" in e for e in errors) and any("/using/nope/" in e for e in errors), errors
        assert any("../ticker/" in e for e in errors), errors

        # Under a base, links outside it fail.
        (dist / "index.html").write_text(
            '<a href="/docs/using/weather/#radar">ok</a><a href="using/weather/">rel</a>'
            '<a href="/using/weather/">no base</a><img src="/docs/favicon.svg"><img src="/docs/gone.png">'
            '<link rel="stylesheet" href="/docs/x.css"><script src="data:,1"></script>')
        (dist / "using" / "weather" / "index.html").write_text('<h2 id="radar">Radar</h2>')
        errors = check(dist, "/docs/")
        assert len(errors) == 3, errors
        assert any("/using/weather/ goes" in e for e in errors), errors
        assert any("gone.png" in e for e in errors) and any("x.css" in e for e in errors), errors
    print("check_docs_links selftest ok")


def main(argv: list) -> int:
    if argv[:1] == ["--selftest"]:
        selftest()
        return 0
    base = ""
    if argv[:1] == ["--base"] and len(argv) > 1:
        base, argv = argv[1], argv[2:]
    if len(argv) != 1 or not Path(argv[0]).is_dir():
        print(__doc__)
        return 2
    dist = Path(argv[0])
    errors = check(dist, base)
    for e in errors:
        print(f"::error::Broken docs link: {e}")
    n = len(list(dist.rglob("*.html")))
    print(f"{n} pages, {len(errors)} broken links")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
