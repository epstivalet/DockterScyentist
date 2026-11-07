#!/usr/bin/env python3
"""Generate a download allowlist from the two user-selected Archive directories.

Only names and HTTPS URLs are embedded: no game content is bundled. An
unavailable/empty directory fails the build rather than silently advertising
downloads that have not been indexed.
"""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import quote, unquote, urljoin, urlsplit
from urllib.request import Request, urlopen
import time

SOURCES = {
    "GC": ("https://archive.org/download/gamecubegames/Nintendo%20GameCube/",
           (".iso", ".gcm", ".rvz")),
    "SNES": ("https://archive.org/download/super-nintendo-snes-rom-collection-usa/",
             (".zip", ".sfc", ".smc")),
}
OUT = Path(__file__).with_name("archive_data.inc")

class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs = []
    def handle_starttag(self, tag, attrs):
        if tag.lower() == "a":
            href = dict(attrs).get("href")
            if href:
                self.hrefs.append(href)

def safe_file(name):
    if not name or len(name.encode("utf-8")) > 220 or name[0] == ".":
        return False
    return all(ord(ch) >= 32 and ch not in '/\\\\:<>|"?' for ch in name)

def cstr(s):
    out = '"'
    for b in s.encode("utf-8"):
        if b == 34:
            out += '\\"'
        elif b == 92:
            out += '\\\\'
        elif 32 <= b < 127:
            out += chr(b)
        else:
            out += "\\%03o" % b
    return out + '"'

def fetch(base):
    last = None
    for attempt in range(3):
        try:
            req = Request(base, headers={"User-Agent": "Mozilla/5.0 RetroVerse/0.11"})
            with urlopen(req, timeout=50) as resp:
                data = resp.read(16 * 1024 * 1024 + 1)
            if len(data) > 16 * 1024 * 1024:
                raise ValueError("Directory index exceeds size limit")
            return data.decode("utf-8", "replace")
        except Exception as e:
            last = e
            time.sleep(attempt + 1)
    raise RuntimeError(f"Cannot fetch the Archive directory {base}: {last}")

def list_files(base, suffixes):
    links = Links()
    links.feed(fetch(base))
    base_path = unquote(urlsplit(base).path.rstrip("/"))
    found = set()
    for href in links.hrefs:
        full = urljoin(base, href)
        url = urlsplit(full)
        if url.scheme != "https" or url.netloc.lower() != "archive.org":
            continue
        path = unquote(url.path)
        if path.rsplit("/", 1)[0] != base_path:
            continue
        name = path.rsplit("/", 1)[-1]
        if safe_file(name) and name.lower().endswith(suffixes):
            found.add(name)
    if len(found) < 10:
        raise RuntimeError(f"Archive listing yielded only {len(found)} valid files from {base}")
    return sorted(found, key=str.casefold)

def main():
    rows = []
    for platform, (base, suffixes) in SOURCES.items():
        for name in list_files(base, suffixes):
            # URLs are generated only from the fixed, selected HTTPS directories.
            url = base + quote(name, safe="")
            rows.append(f"    {{{cstr(platform)}, {cstr(name)}, {cstr(url)}}},")
        print(platform, "indexed", sum(cstr(platform) in x for x in rows), "files", flush=True)
    source = [
        "/* GENERATED from the two user-provided archive.org directories. */",
        "typedef struct { const char *platform; const char *filename; const char *url; } archive_source_t;",
        "static const archive_source_t archive_sources[] = {",
        *rows,
        "};",
        "static const size_t archive_sources_count =",
        "    sizeof archive_sources / sizeof archive_sources[0];",
    ]
    OUT.write_text("\n".join(source) + "\n", encoding="utf-8")
    print("Wrote", len(rows), "allowlisted entries to", OUT, flush=True)

if __name__ == "__main__":
    main()
