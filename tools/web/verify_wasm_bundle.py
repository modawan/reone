#!/usr/bin/env python3
"""Fail the build if Emscripten did not emit a complete wasm bundle under DIR.

CMake invokes this after linking ``engine`` so a broken/partial link cannot leave
``bin/`` looking like “only engine.data” while you assume the tree is good.
"""
from __future__ import annotations

import pathlib
import sys

MIN_WASM = 32768
MIN_HTML = 200
MIN_JS = 500
_NAMES = ("engine.html", "engine.js", "engine.wasm", "engine.data")


def _check_dir(d: pathlib.Path) -> list[str]:
    errs: list[str] = []
    for n in _NAMES:
        p = d / n
        if not p.is_file():
            errs.append(f"verify_wasm_bundle: missing {p}")
            continue
        try:
            sz = p.stat().st_size
        except OSError as e:
            errs.append(f"verify_wasm_bundle: cannot stat {p}: {e}")
            continue
        if n == "engine.wasm" and sz < MIN_WASM:
            errs.append(f"verify_wasm_bundle: {p} is only {sz} bytes (link failed or file locked?)")
        if n == "engine.html" and sz < MIN_HTML:
            errs.append(f"verify_wasm_bundle: {p} looks truncated ({sz} bytes)")
        if n == "engine.js" and sz < MIN_JS:
            errs.append(f"verify_wasm_bundle: {p} looks truncated ({sz} bytes)")
    return errs


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: verify_wasm_bundle.py DIR [DIR...]", file=sys.stderr)
        return 2
    all_errs: list[str] = []
    for arg in sys.argv[1:]:
        root = pathlib.Path(arg).expanduser().resolve()
        if not root.is_dir():
            all_errs.append(f"verify_wasm_bundle: not a directory {root}")
            continue
        all_errs.extend(_check_dir(root))
    if all_errs:
        print("\n".join(all_errs), file=sys.stderr)
        print(
            "\nFix: close browsers / serve.py holding the wasm open, clear AV locks, "
            "then rebuild target ``engine``. On Windows see ``reone-web-output-dir.txt`` "
            "next to your build dir if outputs were redirected to %%TEMP%%.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
