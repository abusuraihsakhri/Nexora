#!/usr/bin/env python3
"""Dependency-free validation for the Nexora GitHub Pages static site."""
from html.parser import HTMLParser
from pathlib import Path

DOCS = (Path("docs/index.html"), Path("docs/boot-sim.html"))
REQUIRED_ASSETS = (
    Path("docs/academia.css"),
    Path("docs/site.js"),
    Path("docs/boot-sim.js"),
)


class Parser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.title_depth = 0
        self.title: list[str] = []
        self.ids: set[str] = set()
        self.links: list[str] = []
        self.assets: list[str] = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "title":
            self.title_depth += 1

        value = attrs.get("id")
        if value:
            if value in self.ids:
                raise ValueError(f"duplicate id: {value}")
            self.ids.add(value)

        if tag == "a" and attrs.get("href"):
            self.links.append(attrs["href"])

        if tag == "link" and attrs.get("href"):
            self.assets.append(attrs["href"])

        if tag == "script" and attrs.get("src"):
            self.assets.append(attrs["src"])

    def handle_endtag(self, tag):
        if tag == "title" and self.title_depth:
            self.title_depth -= 1

    def handle_data(self, data):
        if self.title_depth:
            self.title.append(data)


def is_external(value: str) -> bool:
    return value.startswith(("http://", "https://", "mailto:", "tel:"))


def resolve_local(page: Path, target: str) -> Path:
    clean = target.split("#", 1)[0].split("?", 1)[0]
    return (page.parent / clean).resolve()


def main() -> int:
    errors: list[str] = []

    for asset in REQUIRED_ASSETS:
        if not asset.is_file():
            errors.append(f"missing required asset: {asset}")

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
            if href.startswith("#"):
                fragment = href[1:]
                if fragment and fragment not in parser.ids:
                    errors.append(f"{path}: broken fragment {href}")
                continue

            if is_external(href):
                continue

            target, _, fragment = href.partition("#")
            if target:
                local = resolve_local(path, target)
                if not local.is_file():
                    errors.append(f"{path}: missing local link target {target}")
                    continue

                if fragment and local.suffix.lower() == ".html":
                    linked = Parser()
                    try:
                        linked.feed(local.read_text(encoding="utf-8"))
                        linked.close()
                        if fragment not in linked.ids:
                            errors.append(f"{path}: broken cross-page fragment {href}")
                    except Exception as exc:
                        errors.append(f"{path}: could not validate {href}: {exc}")

        for asset in parser.assets:
            if is_external(asset):
                continue
            if not resolve_local(path, asset).is_file():
                errors.append(f"{path}: missing local asset {asset}")

    if errors:
        print("documentation validation FAILED")
        for error in errors:
            print(" -", error)
        return 1

    print("documentation validation PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
