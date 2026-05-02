#!/usr/bin/env python3
"""Load engine.html locally and poll until the menu likely appeared or timeout."""
from __future__ import annotations

import argparse
import asyncio
import os
import sys


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
        browser = await p.chromium.launch(
            headless=not headed,
            args=[
                "--ignore-gpu-blocklist",
                "--use-gl=angle",
                "--disable-dev-shm-usage",
            ],
        )
        context = await browser.new_context(
            viewport={"width": 1280, "height": 720},
            ignore_https_errors=True,
        )
        page = await context.new_page()

        def _on_console(msg) -> None:
            try:
                print(f"[console.{msg.type}] {msg.text}")
            except Exception:
                pass

        page.on("console", _on_console)
        page.on("pageerror", lambda exc: print(f"[pageerror] {exc}"))

        print(f"Navigating {url} … (goto timeout {goto_ms} ms, headed={headed})", flush=True)
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

            low = (outp or "").lower()
            if "engine failure:" in low:
                print("Engine reported failure; saving screenshot and exiting.", flush=True)
                await page.screenshot(path=out_png, full_page=True)
                await browser.close()
                return 2

            # Logger prints: " INFO [main][global] reone smoke signal: engine startup"
            if "reone smoke signal: engine startup" in low:
                print("Engine startup log seen; waiting for main menu draw…", flush=True)
                await asyncio.sleep(35.0)
                await page.screenshot(path=out_png, full_page=True)
                await browser.close()
                return 0

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

            # Do not gl.readPixels here — it can stall or lose the WebGL context in headless Chromium.

            await asyncio.sleep(interval_s)
            i += 1

        print("Timeout — saving final screenshot.", flush=True)
        await page.screenshot(path=out_png, full_page=True)
        await browser.close()
        return 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:4204/engine.html")
    ap.add_argument("--out", default="tools/web/_smoke_menu.png")
    ap.add_argument("--timeout", type=float, default=1800.0, help="seconds (default 30 min)")
    ap.add_argument("--interval", type=float, default=10.0)
    args = ap.parse_args()
    return asyncio.run(run(args.url, args.out, args.timeout, args.interval))


if __name__ == "__main__":
    raise SystemExit(main())
