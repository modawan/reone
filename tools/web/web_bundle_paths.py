# Copyright (c) 2020-2023 The reone project contributors
"""Locate ``reone-web-output-dir.txt`` and the real Emscripten output dir (Windows wasm redirect)."""

from __future__ import annotations

import pathlib


def find_reone_web_output_stamp(web_bin: pathlib.Path) -> pathlib.Path | None:
    """Return the nearest ``reone-web-output-dir.txt`` walking up from ``web_bin``."""
    cur = web_bin.expanduser().resolve()
    for _ in range(12):
        for base in (cur.parent, cur):
            stamp = base / "reone-web-output-dir.txt"
            if stamp.is_file():
                return stamp
        if cur.parent == cur:
            break
        cur = cur.parent
    return None


def resolve_web_build_directory(directory: pathlib.Path) -> pathlib.Path:
    """If CMake recorded a Windows wasm redirect, serve from that folder when it has ``engine.html``."""
    d = directory.expanduser().resolve()
    stamp = find_reone_web_output_stamp(d)
    if stamp is None:
        return d
    try:
        raw = stamp.read_text(encoding="utf-8").strip()
        if not raw:
            return d
        alt = pathlib.Path(raw).expanduser().resolve()
        if alt.is_dir() and (alt / "engine.html").is_file():
            return alt
    except OSError:
        pass
    return d
