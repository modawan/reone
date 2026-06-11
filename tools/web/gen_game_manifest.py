#!/usr/bin/env python3
# Copyright (c) 2020-2023 The reone project contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Emit game-manifest.json for serve.py --game-root; gamefs.js loads it when present (dev/CI). retail flow is File System Access only."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys


def _load_paths(paths_file: pathlib.Path) -> list[str]:
    lines = paths_file.read_text(encoding="utf-8").splitlines()
    out: list[str] = []
    for line in lines:
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        out.append(s.replace("\\", "/"))
    return out


def _walk_game_files(game_root: pathlib.Path) -> list[str]:
    """All files under game_root (KotOR install layout); relative posix paths."""
    out: list[str] = []
    for p in game_root.rglob("*"):
        if not p.is_file():
            continue
        rel = p.relative_to(game_root).as_posix()
        parts = rel.split("/")
        if any(part.startswith(".") for part in parts):
            continue
        out.append(rel)
    return sorted(out)


def _parent_directories(rel_paths: list[str]) -> list[str]:
    seen: set[str] = set()
    ordered: list[str] = []
    for rel in rel_paths:
        parent = str(pathlib.PurePosixPath(rel).parent)
        if parent in (".", ""):
            continue
        parts = parent.split("/")
        for i in range(1, len(parts) + 1):
            prefix = "/".join(parts[:i])
            if prefix not in seen:
                seen.add(prefix)
                ordered.append(prefix)
    return ordered


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate game-manifest.json for optional reone lazy HTTP FS (dev/CI only)"
    )
    parser.add_argument(
        "game_root",
        type=pathlib.Path,
        help="Directory containing KotOR files (e.g. Steam KotOR install)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=pathlib.Path,
        default=None,
        help="Write manifest here (default: <game-root>/game-manifest.json)",
    )
    parser.add_argument(
        "--paths-file",
        type=pathlib.Path,
        default=None,
        help="Optional newline list of relative paths; default is to include every file under game_root",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="With --paths-file: exit with error if any listed file is missing",
    )
    args = parser.parse_args()

    game_root = args.game_root.resolve()
    if not game_root.is_dir():
        print(f"Not a directory: {game_root}", file=sys.stderr)
        return 1

    if args.paths_file:
        paths_file = args.paths_file.resolve()
        if not paths_file.is_file():
            print(f"Paths file not found: {paths_file}", file=sys.stderr)
            return 1
        rel_paths = _load_paths(paths_file)
        missing: list[str] = []
        confirmed: list[str] = []
        for rel in rel_paths:
            p = game_root / rel
            if p.is_file():
                confirmed.append(rel.replace("\\", "/"))
            else:
                missing.append(rel)
        if args.strict and missing:
            print("Missing files:", file=sys.stderr)
            for m in missing:
                print(f"  {m}", file=sys.stderr)
            return 2
    else:
        confirmed = _walk_game_files(game_root)
        missing = []

    manifest = {
        "directories": _parent_directories(confirmed),
        "files": confirmed,
    }
    if missing:
        manifest["missing"] = missing

    out_path = args.output or (game_root / "game-manifest.json")
    out_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {out_path} ({len(confirmed)} files)")
    if missing:
        print(f"Warning: {len(missing)} paths skipped (not found)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())