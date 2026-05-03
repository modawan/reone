#!/usr/bin/env python3
"""
Copy a reduced KotOR 1 tree into ``out_dir`` for faster WASM preload while still
reaching the main menu (see ResourceDirector::loadGlobalResources + Game::init module scan).

Still dominated by ``data/`` (~1.2+ GiB). **Texture packs:** only the GUI ERF plus **one**
world pack (high/medium/low) are copied — saves hundreds of MB vs copying all ``swpc_tex_t*.erf``.
Omit ``movies/``, ``override/``, ``saves/``, and (by default) ``streamwaves/``.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


def _copy_path(src: Path, dst_root: Path, rel: str) -> bool:
    s = src / rel
    if not s.exists():
        return False
    d = dst_root / rel
    d.parent.mkdir(parents=True, exist_ok=True)
    if s.is_dir():
        shutil.copytree(s, d, dirs_exist_ok=True)
    else:
        shutil.copy2(s, d)
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description="Stage minimal KotOR tree for web preload")
    ap.add_argument("game_root", type=Path, help="Retail KotOR install (contains chitin.key, swkotor.exe)")
    ap.add_argument("out_dir", type=Path, help="Output directory (created/overwritten)")
    ap.add_argument(
        "--with-streamwaves",
        action="store_true",
        help="Also copy streamwaves/ (~1 GiB); omit by default if menu loads without it",
    )
    ap.add_argument(
        "--texture-quality",
        choices=("high", "medium", "low"),
        default="high",
        help="Which world texture pack to include (must match engine tex quality; default high = swpc_tex_tpa.erf)",
    )
    args = ap.parse_args()
    src = args.game_root.resolve()
    if not (src / "chitin.key").is_file():
        print(f"error: missing chitin.key under {src}", file=sys.stderr)
        return 2

    dst = args.out_dir.resolve()
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)

    tex_pack_by_quality = {
        "high": "swpc_tex_tpa.erf",
        "medium": "swpc_tex_tpb.erf",
        "low": "swpc_tex_tpc.erf",
    }
    world_pack = tex_pack_by_quality[args.texture_quality]

    copied = []
    for rel in (
        "swkotor.exe",
        "chitin.key",
        "dialog.tlk",
        "patch.erf",
        "data",
    ):
        if _copy_path(src, dst, rel):
            copied.append(rel)

    tp_src = src / "texturepacks"
    tp_dst = dst / "texturepacks"
    tp_dst.mkdir(parents=True, exist_ok=True)
    gui_name = "swpc_tex_gui.erf"
    for name in (gui_name, world_pack):
        f = tp_src / name
        if f.is_file():
            shutil.copy2(f, tp_dst / name)
            copied.append(f"texturepacks/{name}")
        else:
            print(f"error: missing {f}", file=sys.stderr)
            return 2

    # ResourceDirector::loadGlobalResources only mounts these two lip packs initially.
    # Omit streammusic/streamsounds/streamwaves here — fewer preload files → faster WASM startup;
    # menu graphics still load from BIFs + texture packs.
    lips_dst = dst / "lips"
    lips_dst.mkdir(parents=True, exist_ok=True)
    lips_src = src / "lips"
    if lips_src.is_dir():
        for name in ("global.mod", "localization.mod", "mainmenu.mod"):
            p = lips_src / name
            if p.is_file():
                shutil.copy2(p, lips_dst / name)
                copied.append(f"lips/{name}")

    if args.with_streamwaves:
        if _copy_path(src, dst, "streamwaves"):
            copied.append("streamwaves")

    modules = dst / "modules"
    modules.mkdir(parents=True, exist_ok=True)
    copied.append("modules/ (empty)")

    exe = dst / "swkotor.exe"
    if not exe.is_file():
        print("error: swkotor.exe not copied — need KotOR 1 install", file=sys.stderr)
        return 2

    print("Staged:", dst)
    for c in copied:
        print(" ", c)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
