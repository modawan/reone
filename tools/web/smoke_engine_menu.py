#!/usr/bin/env python3
"""Load engine.html locally and poll until the menu likely appeared or timeout.

ship no game data in the wasm bundle; point the browser at a local
install via File System Access *or* run ``serve.py --game-root`` so ``gamefs.js`` can
fetch ``/game-manifest.json`` + ``/game-files/...`` (dev/CI only).
"""
from __future__ import annotations

import argparse
import asyncio
import json
from collections import deque
import os
import pathlib
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


async def run(url: str, out_png: str, timeout_s: float, interval_s: float) -> int:
    try:
        from playwright.async_api import async_playwright
    except ImportError:
        print("Install: pip install playwright && python -m playwright install chromium", file=sys.stderr)
        return 2

    deadline = asyncio.get_event_loop().time() + timeout_s
    headed = os.environ.get("PLAYWRIGHT_HEADED", "").strip() in ("1", "true", "yes")
    goto_ms = min(600_000, max(120_000, int(timeout_s * 1000)))

    async with async_playwright() as p:
        browser_name = (os.environ.get("REONE_WEB_SMOKE_BROWSER") or "chromium").strip().lower()
        if browser_name == "firefox":
            browser = await p.firefox.launch(
                headless=not headed,
            )
        else:
            browser = await p.chromium.launch(
                headless=not headed,
                args=[
                    "--ignore-gpu-blocklist",
                    "--use-gl=angle",
                    "--disable-dev-shm-usage",
                    "--disable-http-cache",
                ],
            )
        context = await browser.new_context(
            viewport={"width": 1280, "height": 720},
            ignore_https_errors=True,
        )
        page = await context.new_page()

        # Engine sometimes logs menu transitions only to console, not #output.
        console_lines: deque[str] = deque(maxlen=500)

        def _on_console(msg) -> None:
            try:
                t = msg.text
                console_lines.append(t)
                print(f"[console.{msg.type}] {t}")
            except Exception:
                pass

        page.on("console", _on_console)
        page.on("pageerror", lambda exc: print(f"[pageerror] {exc}"))

        game_manifest_503 = {"hit": False}

        def _on_response(resp) -> None:
            try:
                u = resp.url
                if u.rstrip("/").endswith("game-manifest.json") and resp.status == 503:
                    game_manifest_503["hit"] = True
            except Exception:
                pass

        page.on("response", _on_response)

        print(f"Navigating {url} ... (goto timeout {goto_ms} ms, headed={headed})", flush=True)
        await page.goto(url, wait_until="domcontentloaded", timeout=goto_ms)

        i = 0
        while asyncio.get_event_loop().time() < deadline:
            try:
                status = await page.locator("#status").inner_text(timeout=5000)
            except Exception:
                status = "(no #status)"
            try:
                outp = await page.locator("#output").inner_text(timeout=2000)
                tail = outp[-400:] if outp else ""
            except Exception:
                tail = ""
            print(f"[{i * interval_s:.0f}s] status={status!r}", flush=True)
            if tail.strip():
                print(f"  output tail: {tail!r}", flush=True)
            if "Exception" in status and (outp or "").strip():
                print("--- #output (full) ---", flush=True)
                print((outp or "").strip(), flush=True)
                print("--- end #output ---", flush=True)

            console_tail = "\n".join(console_lines)
            combined_log = ((outp or "") + "\n" + console_tail).lower()
            low = combined_log
            if "engine failure:" in low:
                print("Engine reported failure; saving screenshot and exiting.", flush=True)
                await page.screenshot(path=out_png, full_page=True)
                await browser.close()
                return 2

            if any(
                x in low
                for x in (
                    "main menu",
                    "mainmenu16x12",
                    "opening main menu",
                )
            ):
                print("Detected menu-related log; capturing screenshot.", flush=True)
                await page.screenshot(path=out_png, full_page=True)
                await browser.close()
                return 0

            if game_manifest_503["hit"] and i >= 1:
                print(
                    "GET /game-manifest.json returned 503 (old serve.py). "
                    "Retail files stay on disk; use current serve.py + --game-root or File System Access.",
                    flush=True,
                )
                await page.screenshot(path=out_png, full_page=True)
                await browser.close()
                return 3

            # Do not gl.readPixels here - it can stall or lose the WebGL context in headless Chromium.

            await asyncio.sleep(interval_s)
            i += 1

        print("Timeout - saving final screenshot.", flush=True)
        await page.screenshot(path=out_png, full_page=True)
        await browser.close()
        return 1


def _url_is_picker_mode(url: str) -> bool:
    """True when ``?reoneFs=`` requests File System Access only (no HTTP mirror probe)."""
    try:
        parsed = urllib.parse.urlparse(url)
        qs = urllib.parse.parse_qs(parsed.query)
        v = (qs.get("reoneFs") or [""])[0].strip().lower()
        return v in ("picker", "fs", "access")
    except Exception:
        return False


def _free_port() -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _preflight_http(url: str) -> int:
    """Return 0 if ``url`` responds with HTTP; 4 on connection/empty response; 3 if HTTP mirror has no files."""
    try:
        urllib.request.urlopen(url, timeout=15)
    except urllib.error.HTTPError as e:
        if e.code in (301, 302, 303, 307, 308):
            return 0
        print(f"Preflight GET {url!r} failed: HTTP {e.code}", file=sys.stderr)
        return 4
    except urllib.error.URLError as e:
        print(
            f"Preflight: cannot load {url!r}: {e.reason!r}\n"
            "On Windows, several processes bound to the same port with SO_REUSEADDR can yield "
            "empty HTTP responses (Playwright: net::ERR_EMPTY_RESPONSE). Stop other listeners, "
            "or use a free port. serve.py now defaults to 127.0.0.1 and disables reuse on Windows.",
            file=sys.stderr,
        )
        return 4
    if _url_is_picker_mode(url):
        return 0
    manifest = urllib.parse.urljoin(url, "/game-manifest.json")
    try:
        r = urllib.request.urlopen(manifest, timeout=15)
        raw = r.read()
        try:
            data = json.loads(raw.decode())
            files = data.get("files") if isinstance(data, dict) else None
            if isinstance(files, list) and len(files) == 0:
                print(
                    f"Preflight: {manifest} lists zero files (File System Access mode only). "
                    "For automated smoke / lazy HTTP mirror run:\n"
                    "  py -3 tools/web/gen_game_manifest.py \"C:/Path/To/KotOR\"\n"
                    "  py -3 tools/web/serve.py --directory …/bin --game-root \"C:/Path/To/KotOR\"",
                    file=sys.stderr,
                )
                return 3
        except json.JSONDecodeError:
            pass
    except urllib.error.HTTPError as e:
        if e.code == 503:
            print(
                f"Preflight: {manifest} returns 503 (legacy server). "
                "Update serve.py or pass --game-root for a real manifest.",
                file=sys.stderr,
            )
            return 3
    except urllib.error.URLError:
        pass
    return 0


def _spawn_local_mirror(web_bin: pathlib.Path, game_root: pathlib.Path, port: int) -> subprocess.Popen:
    repo_root = pathlib.Path(__file__).resolve().parents[2]
    gen = repo_root / "tools" / "web" / "gen_game_manifest.py"
    serve = repo_root / "tools" / "web" / "serve.py"
    subprocess.run([sys.executable, str(gen), str(game_root)], check=True)
    return subprocess.Popen(
        [
            sys.executable,
            str(serve),
            "--directory",
            str(web_bin),
            "--game-root",
            str(game_root),
            "--port",
            str(port),
        ],
    )


def main() -> int:
    ap = argparse.ArgumentParser(
        description=(
            "Playwright poll until engine log hints at main menu. "
            "Two setups: (1) Retail FS Access — serve.py WITHOUT --game-root, URL with ?reoneFs=picker "
            "(cannot automate native folder dialog in headless). "
            "(2) CI / automation — --spawn-serve + --game-root (or REONE_WEB_SMOKE_GAME_ROOT only for this script)."
        ),
    )
    ap.add_argument(
        "--url",
        default="http://127.0.0.1:4204/engine.html?reoneFs=picker",
        help="engine.html URL; add ?reoneFs=picker when not using --game-root (skips HTTP mirror in gamefs.js)",
    )
    ap.add_argument("--out", default="tools/web/_smoke_menu.png")
    ap.add_argument("--timeout", type=float, default=1800.0, help="seconds (default 30 min)")
    ap.add_argument("--interval", type=float, default=10.0)
    ap.add_argument(
        "--spawn-serve",
        action="store_true",
        help="Start tools/web/serve.py on a free port (needs --game-root)",
    )
    ap.add_argument(
        "--game-root",
        type=pathlib.Path,
        default=None,
        help="Retail KotOR dir (chitin.key + swkotor.exe); also reads REONE_WEB_SMOKE_GAME_ROOT",
    )
    ap.add_argument(
        "--web-bin",
        type=pathlib.Path,
        default=pathlib.Path("build-web/bin"),
        help="Directory containing engine.html",
    )
    args = ap.parse_args()

    url = args.url
    proc: subprocess.Popen | None = None
    game_root = args.game_root or (
        pathlib.Path(os.environ["REONE_WEB_SMOKE_GAME_ROOT"])
        if os.environ.get("REONE_WEB_SMOKE_GAME_ROOT")
        else None
    )

    if args.spawn_serve:
        if not game_root or not game_root.is_dir():
            print(
                "Smoke --spawn-serve requires --game-root or REONE_WEB_SMOKE_GAME_ROOT "
                "pointing at a KotOR install (files stay on disk).",
                file=sys.stderr,
            )
            return 2
        port = _free_port()
        proc = _spawn_local_mirror(args.web_bin.resolve(), game_root.resolve(), port)
        time.sleep(2.0)
        if proc.poll() is not None:
            print("serve.py exited early (check --web-bin and port).", file=sys.stderr)
            return 2
        url = f"http://127.0.0.1:{port}/engine.html"
        print(f"Serving wasm from {args.web_bin} with mirror {game_root} -> {url}", flush=True)

    pre = _preflight_http(url)
    if pre != 0:
        return pre

    try:
        return asyncio.run(run(url, args.out, args.timeout, args.interval))
    finally:
        if proc is not None:
            proc.terminate()
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
