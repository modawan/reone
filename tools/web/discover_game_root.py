#!/usr/bin/env python3
"""Print the first KotOR install root found (retail chitin.key; swkotor.exe optional)."""
from __future__ import annotations

import argparse
import os
import pathlib
import sys


def _is_kotor_root(p: pathlib.Path) -> bool:
    """Retail KotOR 1 — KEY + TLK; matches native resource probe (exe optional)."""
    key = p / "chitin.key"
    tlk = p / "dialog.tlk"
    if not key.is_file():
        return False
    try:
        if key.stat().st_size < 64 * 1024:
            return False
    except OSError:
        return False
    if tlk.is_file():
        try:
            sig = tlk.read_bytes()[:8]
        except OSError:
            return False
        if sig == b"TLK V1  ":
            return False
        if sig != b"TLK V3.0":
            return False
    return True


def _candidates() -> list[pathlib.Path]:
    out: list[pathlib.Path] = []
    env = os.environ.get("REONE_WEB_SMOKE_GAME_ROOT") or os.environ.get("REONE_GAME_PATH")
    if env:
        out.append(pathlib.Path(env))
    home = pathlib.Path.home()
    patterns = (
        home / ".steam/steam/steamapps/common/swkotor",
        home / ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/swkotor",
        home / ".local/share/Steam/steamapps/common/swkotor",
        pathlib.Path("/mnt/c/Program Files (x86)/Steam/steamapps/common/swkotor"),
        pathlib.Path("/mnt/c/GOG Games/Star Wars - KotOR"),
        pathlib.Path("/mnt/c/Program Files (x86)/LucasArts/KOTOR"),
        pathlib.Path("/mnt/d/Steam/steamapps/common/swkotor"),
        pathlib.Path("/run/media/brunner56/MyBook/SteamLibrary/steamapps/common/swkotor"),
    )
    out.extend(patterns)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Discover a KotOR 1 install for web smokes")
    ap.add_argument("--export", action="store_true", help="Print export REONE_WEB_SMOKE_GAME_ROOT=…")
    args = ap.parse_args()
    for cand in _candidates():
        try:
            root = cand.expanduser().resolve()
        except OSError:
            continue
        if _is_kotor_root(root):
            if args.export:
                print(f'export REONE_WEB_SMOKE_GAME_ROOT="{root}"')
            else:
                print(root)
            return 0
    print("No KotOR install found (need chitin.key >= 64 KiB, dialog.tlk TLK V3.0).", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
