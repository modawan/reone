#!/usr/bin/env python3
"""Inject Module.reoneAssumeEmbeddedGamePreload into Emscripten's engine.html (CMake embedded /game preload)."""
from __future__ import annotations

import pathlib
import sys

MARKER = "reoneAssumeEmbeddedGamePreload"
INJECT = "reoneAssumeEmbeddedGamePreload:!0,"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_engine_embedded_preload.py <engine.html path>", file=sys.stderr)
        return 2
    path = pathlib.Path(sys.argv[1])
    text = path.read_text(encoding="utf-8")
    if MARKER in text:
        return 0
    # Minified shells use Module={print — insert flag immediately after Module={
    needle = "Module={"
    idx = text.find(needle)
    if idx == -1:
        needle = "var Module={"
        idx = text.find(needle)
        if idx == -1:
            print("patch_engine_embedded_preload: Module={ not found", file=sys.stderr)
            return 1
        insert_at = idx + len(needle)
        patched = text[:insert_at] + INJECT + text[insert_at:]
    else:
        insert_at = idx + len(needle)
        patched = text[:insert_at] + INJECT + text[insert_at:]
    path.write_text(patched, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
