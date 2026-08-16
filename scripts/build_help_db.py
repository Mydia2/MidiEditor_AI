#!/usr/bin/env python3
"""Build run_environment/help_db.json from manual/*.html (Phase 44).

The manual is the ONLY truth: this script chunks every page by its h1/h2
anchors and emits a compact JSON the in-app HelpDatabase loads (embedded
via resources.qrc). MidiPilot's search_help / get_help_section tools answer
"how does the editor work" questions from these chunks instead of guessing.

Run it whenever the manual changed (it is part of the release checklist,
right next to build_changelog.py):

    python scripts/build_help_db.py

Stdlib only - no BeautifulSoup, no embeddings. Sibling of
build_changelog.py; keep the same no-dependencies rule.
"""

import json
import os
import re
import sys
from html.parser import HTMLParser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANUAL_DIR = os.path.join(ROOT, "manual")
OUT_PATH = os.path.join(ROOT, "run_environment", "help_db.json")

# Pages that are navigation/infrastructure, not help content.
SKIP_PAGES = {
    "404.html",
    "docs-index.html",
    "download.html",  # version-specific marketing page
    "changelog.html",  # generated, huge, versioned - not "how do I"
    "index.html",  # marketing landing page; feature docs live in the pages
}
SKIP_PREFIXES = ("google",)


class SectionExtractor(HTMLParser):
    """Splits one manual page into (anchor, title, text) sections.

    A section starts at <h1> (anchor "") or <h2 id="...">. Everything up to
    the next heading belongs to it. nav/script/style/footer subtrees are
    ignored entirely.
    """

    IGNORED_CONTAINERS = {"nav", "script", "style", "footer", "head"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.sections = []  # [ {anchor, title, parts:[str]} ]
        self._ignore_depth = 0
        self._heading = None  # "h1" | "h2" while inside one
        self._pending_anchor = None

    def handle_starttag(self, tag, attrs):
        if tag in self.IGNORED_CONTAINERS:
            self._ignore_depth += 1
            return
        if self._ignore_depth:
            return
        if tag in ("h1", "h2"):
            self._heading = tag
            anchor = ""
            for k, v in attrs:
                if k == "id":
                    anchor = v
            self._pending_anchor = anchor
            self.sections.append({"anchor": anchor, "title": "", "parts": []})
        elif tag in ("li",):
            # List items read best with a separator in flat text.
            if self.sections:
                self.sections[-1]["parts"].append(" - ")

    def handle_endtag(self, tag):
        if tag in self.IGNORED_CONTAINERS:
            self._ignore_depth = max(0, self._ignore_depth - 1)
            return
        if tag in ("h1", "h2"):
            self._heading = None
        if self._ignore_depth:
            return
        if tag in ("p", "tr", "ul", "table", "h3"):
            if self.sections:
                self.sections[-1]["parts"].append("\n")
        elif tag in ("td", "th"):
            # HELPDB-TABLES-001: without a cell separator adjacent cells
            # glue into one word ("Tool AccessNone") and poison the index.
            if self.sections:
                self.sections[-1]["parts"].append(" | ")

    def handle_data(self, data):
        if self._ignore_depth or not self.sections:
            return
        if self._heading:
            self.sections[-1]["title"] += data
        else:
            self.sections[-1]["parts"].append(data)


def clean(text):
    text = re.sub(r"[ \t]+", " ", text)
    text = re.sub(r" ?\n ?", "\n", text)
    text = re.sub(r"(?: ?\| ?)+\n", "\n", text)  # drop row-trailing cell separators
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


def is_forwarding_only(text):
    """True if a section is a block of 'moved to' stubs (an intro line or
    two around them is allowed - the block is still not content)."""
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    if len(lines) < 3:
        return False
    stubs = sum(1 for ln in lines if ln.lower().startswith("this section moved to"))
    return stubs >= 3 and stubs * 2 > len(lines)  # a majority of stubs


def tokenize(text):
    return sorted(set(re.findall(r"[a-z0-9]{3,}", text.lower())))


def build():
    entries = []
    for name in sorted(os.listdir(MANUAL_DIR)):
        if not name.endswith(".html"):
            continue
        if name in SKIP_PAGES or name.startswith(SKIP_PREFIXES):
            continue
        with open(os.path.join(MANUAL_DIR, name), encoding="utf-8") as f:
            parser = SectionExtractor()
            parser.feed(f.read())

        page_title = ""
        for sec in parser.sections:
            title = clean(sec["title"])
            text = clean("".join(sec["parts"]))
            if not title or not text:
                continue
            if not page_title:
                page_title = title  # the h1 names the page
            # A section that consists solely of forwarding stubs ("This
            # section moved to ...") exists to keep old deep links alive,
            # not to answer questions - as a search hit it is pure noise
            # (the real content is indexed under its new page). Skip it.
            if is_forwarding_only(text):
                continue
            entries.append({
                "page": name,
                "anchor": sec["anchor"],
                "title": title,
                "pageTitle": page_title,
                # Keywords: title + page name tokens - what scoring boosts.
                "keywords": tokenize(title + " " + page_title + " "
                                     + name.replace("-", " ").replace(".html", "")),
                "text": text,
            })
    return entries


def main():
    entries = build()
    if len(entries) < 50:
        print(f"ERROR: only {len(entries)} sections extracted - parser broken?")
        return 1
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"version": 1, "sections": entries}, f,
                  ensure_ascii=False, separators=(",", ":"))
        f.write("\n")
    pages = len({e["page"] for e in entries})
    size = os.path.getsize(OUT_PATH)
    print(f"help_db.json: {len(entries)} sections from {pages} pages, "
          f"{size // 1024} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
