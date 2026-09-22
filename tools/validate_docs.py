#!/usr/bin/env python3
"""Minimal dependency-free validation for the GitHub Pages HTML."""
from html.parser import HTMLParser
from pathlib import Path
import sys

DOCS = (Path("docs/index.html"), Path("docs/boot-sim.html"))


class Parser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.title_depth = 0
        self.title = []
        self.ids: set[str] = set()
        self.links: list[str] = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "title":
            self.title_depth += 1
        if "id" in attrs:
            value = attrs["id"]
            if value in self.ids:
                raise ValueError(f"duplicate id: {value}")
            self.ids.add(value)
        if tag == "a" and attrs.get("href"):
            self.links.append(attrs["href"])

    def handle_endtag(self, tag):
        if tag == "title" and self.title_depth:
            self.title_depth -= 1

    def handle_data(self, data):
        if self.title_depth:
            self.title.append(data)


def main() -> int:
    errors: list[str] = []
    for path in DOCS:
        if not path.is_file():
            errors.append(f"missing: {path}")
            continue
        parser = Parser()
        try:
            parser.feed(path.read_text(encoding="utf-8"))
            parser.close()
        except Exception as exc:
            errors.append(f"{path}: {exc}")
            continue
        if not "".join(parser.title).strip():
            errors.append(f"{path}: missing <title>")
        for href in parser.links:
            if href.startswith("#") and href[1:] and href[1:] not in parser.ids:
                errors.append(f"{path}: broken fragment {href}")
    if errors:
        print("documentation validation FAILED")
        for error in errors:
            print(" -", error)
        return 1
    print("documentation validation PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
