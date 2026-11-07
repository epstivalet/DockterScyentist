#!/usr/bin/env python3
"""CI: fetch exact official release assets and fail on hash, size or access errors."""
import hashlib
from urllib.request import Request, urlopen
from verified_source_build import validated

for row in validated():
    req = Request(row["url"], headers={
        "User-Agent": "RetroVerse-PS5-SourceCheck/1.0",
        "Accept": "application/octet-stream"})
    with urlopen(req, timeout=80) as response:
        assert response.status == 200, (row["path"], response.status)
        digest = hashlib.sha256()
        size = 0
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            size += len(chunk)
            assert size <= row["size"], (row["path"], "oversized")
            digest.update(chunk)
    assert size == row["size"], (row["path"], size, row["size"])
    assert digest.hexdigest() == row["sha256"], (row["path"], "SHA-256 mismatch")
    print("PASS", row["path"], size, digest.hexdigest(), flush=True)
