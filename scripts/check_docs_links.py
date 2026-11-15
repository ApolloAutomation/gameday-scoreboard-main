#!/usr/bin/env python3
"""Every on-site link in the built docs reaches a page and anchor.

    python3 scripts/check_docs_links.py docs/wiki/dist
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
        self.ids, self.links = set(), []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        for key in ("id", "name") if tag == "a" else ("id",):
            if a.get(key):
                self.ids.add(a[key])
        if tag == "a" and a.get("href") is not None:
            self.links.append(a["href"])


def url_of(dist: Path, page: Path) -> str:
    rel = page.relative_to(dist).as_posix()
    return "/" + (rel[: -len("index.html")] if rel.endswith("index.html") else rel)


def target(dist: Path, path: str):
    """The file a site path serves, or None."""
    rel = unquote(path).lstrip("/")
    for cand in (dist / rel, dist / rel / "index.html", dist / (rel.rstrip("/") + ".html")):
        if cand.is_file():
            return cand
    return None


def check(dist: Path) -> list:
    pages, errors = {}, []
    for f in sorted(dist.rglob("*.html")):
        p = Page()
        p.feed(f.read_text(encoding="utf-8", errors="replace"))
        pages[f] = p
    for f, p in pages.items():
        here = url_of(dist, f)
        for href in p.links:
            parts = urlsplit(href)
            if parts.scheme or parts.netloc or href.startswith(("mailto:", "tel:", "javascript:")):
                continue
            url = urlsplit(urljoin(here, href))
            dest = target(dist, url.path)
            where = f"{f.relative_to(dist).as_posix()}: {href}"
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
    print("check_docs_links selftest ok")


def main(argv: list) -> int:
    if argv[:1] == ["--selftest"]:
        selftest()
        return 0
    if len(argv) != 1 or not Path(argv[0]).is_dir():
        print(__doc__)
        return 2
    dist = Path(argv[0])
    errors = check(dist)
    for e in errors:
        print(f"::error::Broken docs link: {e}")
    n = len(list(dist.rglob("*.html")))
    print(f"{n} pages, {len(errors)} broken links")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
