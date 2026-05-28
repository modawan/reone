#!/usr/bin/env python3
"""Print the first KotOR install root found (contains chitin.key + swkotor.exe)."""
from __future__ import annotations

import argparse
import os
import pathlib
import sys


def _is_kotor_root(p: pathlib.Path) -> bool:
    return (p / "chitin.key").is_file() and (p / "swkotor.exe").is_file()


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
        pathlib.Path("/tmp/gemrb-kotor"),
        pathlib.Path(
            "/var/lib/flatpak/app/org.gemrb.gemrb/x86_64/stable/"
            "d8f0a306e9ea69c0ddb88a95a490ad3fa033f09a1e58c8a2b550c9a51a59ea8b/files/share/gemrb/demo"
        ),
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
    print("No KotOR install found (need chitin.key and swkotor.exe).", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
