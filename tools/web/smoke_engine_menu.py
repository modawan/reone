#!/usr/bin/env python3
"""Load engine.html locally and poll until the menu likely appeared or timeout.

ship no game data in the wasm bundle; point the browser at a local
install via File System Access *or* run ``serve.py --game-root`` so ``gamefs.js`` can
fetch ``/game-manifest.json`` + ``/game-files/...`` (dev/CI only).
"""
from __future__ import annotations

import argparse
import asyncio
import base64
import json
from collections import deque
import os
import pathlib
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

_tools_web = pathlib.Path(__file__).resolve().parent
if str(_tools_web) not in sys.path:
    sys.path.insert(0, str(_tools_web))

from discover_game_root import _is_kotor_root


def _emit(*parts: object, **kwargs: object) -> None:
    """Print status lines; ignore EAGAIN when stdout is a full pipe (e.g. piped to tail)."""
    try:
        print(*parts, **kwargs)
    except BlockingIOError:
        pass


from web_bundle_paths import resolve_web_build_directory


async def _canvas_mean_luminance(page) -> float | None:
    """Best-effort: copy the WebGL canvas to a 2D canvas and return mean luminance (0-255).

    Returns None if it cannot be measured (headless can refuse drawImage on a lost/empty
    drawing buffer). A None result is informational only; module-ready logs are authoritative.
    """
    try:
        return await page.evaluate(
            """() => {
                const c = document.querySelector('canvas');
                if (!c) return null;
                const w = Math.min(160, c.width || 160), h = Math.min(90, c.height || 90);
                if (!w || !h) return null;
                const off = document.createElement('canvas');
                off.width = w; off.height = h;
                const ctx = off.getContext('2d');
                if (!ctx) return null;
                ctx.drawImage(c, 0, 0, w, h);
                const d = ctx.getImageData(0, 0, w, h).data;
                let sum = 0;
                for (let i = 0; i < d.length; i += 4) {
                    sum += 0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2];
                }
                return sum / (d.length / 4);
            }"""
        )
    except Exception:
        return None


async def _lazy_io_idle(page) -> bool:
    """Wait until gamefs.js has no in-flight mirror fetches (boot BIF reads can race module load)."""
    try:
        return bool(
            await page.evaluate(
                "() => (typeof Module === 'object' && typeof Module.reoneWebLazyIoIdle === 'function') "
                "? Module.reoneWebLazyIoIdle() : true"
            )
        )
    except Exception:
        return False


async def _module_flag(page, name: str) -> bool:
    """Read a boolean ``Module.<name>`` flag set by the engine's EM_ASM signals.

    The engine routes info() to stdout, not always to the browser console, so log scraping
    is unreliable. gamefs.js exposes authoritative readiness flags (reoneWebMenuReady after
    openMainMenu, reoneWebModuleReady after loadModule + openInGame); probe those directly.
    """
    try:
        return bool(
            await page.evaluate(
                "(n) => (typeof Module === 'object' && Module[n] === true)", name
            )
        )
    except Exception:
        return False


async def run(
    url: str,
    out_png: str,
    timeout_s: float,
    interval_s: float,
    warp_module: str | None = None,
) -> int:
    try:
        from playwright.async_api import async_playwright
    except ImportError:
        venv_py = _tools_web / ".venv" / "bin" / "python"
        if venv_py.is_file():
            _emit(
                f"Playwright missing for {sys.executable}; re-run with: {venv_py} {' '.join(sys.argv)}",
                file=sys.stderr,
            )
        else:
            _emit(
                "Install: cd tools/web && python3 -m venv .venv && .venv/bin/pip install playwright "
                "&& .venv/bin/python -m playwright install chromium",
                file=sys.stderr,
            )
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
                _emit(f"[console.{msg.type}] {t}")
            except Exception:
                pass

        page.on("console", _on_console)

        def _on_pageerror(exc) -> None:
            # exc is a playwright Error; .stack carries the symbolicated wasm frames
            # (needs --profiling-funcs in the wasm link to show C++ names).
            stack = getattr(exc, "stack", None)
            _emit(f"[pageerror] {exc}", flush=True)
            if stack and str(stack).strip() and str(stack).strip() != str(exc).strip():
                _emit("--- pageerror stack ---", flush=True)
                _emit(stack, flush=True)
                _emit("--- end pageerror stack ---", flush=True)

        page.on("pageerror", _on_pageerror)

        game_manifest_503 = {"hit": False}

        def _on_response(resp) -> None:
            try:
                u = resp.url
                if u.rstrip("/").endswith("game-manifest.json") and resp.status == 503:
                    game_manifest_503["hit"] = True
            except Exception:
                pass

        page.on("response", _on_response)

        _emit(f"Navigating {url} ... (goto timeout {goto_ms} ms, headed={headed})", flush=True)
        await page.goto(url, wait_until="domcontentloaded", timeout=goto_ms)

        i = 0
        warp_sent = False
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
            _emit(f"[{i * interval_s:.0f}s] status={status!r}", flush=True)
            if tail.strip():
                _emit(f"  output tail: {tail!r}", flush=True)
            if "Exception" in status and (outp or "").strip():
                _emit("--- #output (full) ---", flush=True)
                _emit((outp or "").strip(), flush=True)
                _emit("--- end #output ---", flush=True)

            console_tail = "\n".join(console_lines)
            combined_log = ((outp or "") + "\n" + console_tail).lower()
            low = combined_log
            if "engine failure:" in low:
                _emit("Engine reported failure; saving screenshot and exiting.", flush=True)
                await page.screenshot(path=out_png, full_page=True)
                await browser.close()
                return 2

            menu_ready = await _module_flag(page, "reoneWebMenuReady") or any(
                x in low
                for x in (
                    "main menu",
                    "mainmenu16x12",
                    "opening main menu",
                )
            )

            # Module (area) verification mode: after the menu is up, warp into a module and
            # confirm the area loads + the in-game screen renders.
            if warp_module:
                if menu_ready and not warp_sent:
                    _emit(
                        f"Menu ready; preloading module files and warping into {warp_module!r}…",
                        flush=True,
                    )
                    try:
                        ok = await page.evaluate(
                            """async (name) => {
                                if (typeof Module !== 'object') return false;
                                if (typeof Module.reoneWebWarpAsync === 'function') {
                                    return await Module.reoneWebWarpAsync(name);
                                }
                                if (typeof Module.reoneWebPreloadModuleFiles === 'function') {
                                    await Module.reoneWebPreloadModuleFiles(name);
                                }
                                return typeof Module.reoneWebWarp === 'function'
                                    ? Module.reoneWebWarp(name)
                                    : false;
                            }""",
                            warp_module,
                        )
                        if not ok:
                            _emit(
                                "Module.reoneWebWarpAsync/Warp failed or export missing.",
                                flush=True,
                            )
                            await page.screenshot(path=out_png, full_page=True)
                            await browser.close()
                            return 2
                    except Exception as e:
                        _emit(f"Failed to invoke reoneWebWarp: {e}", flush=True)
                        await page.screenshot(path=out_png, full_page=True)
                        await browser.close()
                        return 2
                    warp_sent = True

                if warp_sent and (
                    "failed loading module" in low or "loadmodule failed" in low
                ):
                    _emit("Engine reported module load failure; saving screenshot.", flush=True)
                    await page.screenshot(path=out_png, full_page=True)
                    await browser.close()
                    return 2

                module_ready = warp_sent and (
                    await _module_flag(page, "reoneWebModuleReady")
                    or any(
                        x in low
                        for x in (
                            f"module '{warp_module.lower()}' loaded successfully",
                            "module ready (in-game)",
                        )
                    )
                )
                if module_ready:
                    # Give the in-game scene a couple of frames to draw before sampling.
                    await asyncio.sleep(2.0)
                    lum = await _canvas_mean_luminance(page)
                    await page.screenshot(path=out_png, full_page=True)
                    _emit(
                        f"Module {warp_module!r} loaded; in-game. canvas mean luminance="
                        f"{lum if lum is not None else 'n/a'}",
                        flush=True,
                    )
                    if lum is not None and lum < 2.0:
                        _emit(
                            "WARNING: canvas luminance probe is near-black "
                            "(headless WebGL readback is often unreliable); "
                            "trusting Module.reoneWebModuleReady + screenshot.",
                            flush=True,
                        )
                    await browser.close()
                    return 0
                # Not ready yet: keep polling until module loads or timeout.
            elif menu_ready:
                _emit("Detected menu-related log; capturing screenshot.", flush=True)
                await page.screenshot(path=out_png, full_page=True)
                await browser.close()
                return 0

            if game_manifest_503["hit"] and i >= 1:
                _emit(
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

        _emit("Timeout - saving final screenshot.", flush=True)
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


def _localhost_listen_port_from_url(url: str) -> int | None:
    """Return TCP port if ``url`` targets loopback; else None."""
    try:
        p = urllib.parse.urlparse(url)
        host = (p.hostname or "").strip().lower()
        if host not in ("127.0.0.1", "localhost", "::1"):
            return None
        return p.port or (443 if (p.scheme or "").lower() == "https" else 80)
    except Exception:
        return None


def _powershell_encoded_command(script: str) -> list[str]:
    payload = script.encode("utf-16-le")
    enc = base64.b64encode(payload).decode("ascii")
    return [
        "powershell.exe",
        "-NoProfile",
        "-NonInteractive",
        "-ExecutionPolicy",
        "Bypass",
        "-EncodedCommand",
        enc,
    ]


def _kill_windows_tools_web_serve_py() -> None:
    """Terminate any ``tools/web/serve.py`` (or ``tools\\web\\serve.py``) listener left from a crashed smoke run."""
    ps = r"""
$ErrorActionPreference = 'SilentlyContinue'
Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -and (
        $_.CommandLine -match '[\\/]tools[\\/]web[\\/]serve\.py' -or
        ($_.CommandLine -match 'serve\.py' -and $_.CommandLine -match '--game-root')
    )
} | ForEach-Object {
    Write-Host ('[smoke nuke] stopping serve.py PID ' + $_.ProcessId)
    Stop-Process -Id $_.ProcessId -Force
}
"""
    try:
        subprocess.run(
            _powershell_encoded_command(ps),
            capture_output=True,
            text=True,
            timeout=120,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as e:
        _emit(f"[smoke nuke] PowerShell serve.py cleanup failed: {e}", flush=True)


def _kill_unix_tools_web_serve_py() -> None:
    try:
        r = subprocess.run(
            ["pgrep", "-f", "tools/web/serve.py"],
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        return
    for pid_s in r.stdout.strip().split():
        if not pid_s.isdigit():
            continue
        try:
            pid = int(pid_s)
            _emit(f"[smoke nuke] SIGTERM serve.py PID {pid}", flush=True)
            os.kill(pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass


def _kill_windows_listen_port(port: int) -> None:
    try:
        r = subprocess.run(
            ["netstat", "-ano", "-p", "tcp"],
            capture_output=True,
            text=True,
            timeout=60,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as e:
        _emit(f"[smoke nuke] netstat failed: {e}", flush=True)
        return
    needle = f":{port}"
    for line in r.stdout.splitlines():
        if "LISTENING" not in line:
            continue
        parts = line.split()
        if len(parts) < 5:
            continue
        local = parts[1]
        if not local.endswith(needle):
            continue
        pid_s = parts[-1]
        if not pid_s.isdigit():
            continue
        _emit(f"[smoke nuke] taskkill LISTENING :{port} PID {pid_s}", flush=True)
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", pid_s],
            capture_output=True,
            text=True,
            timeout=60,
        )


def _kill_unix_listen_port(port: int) -> None:
    try:
        r = subprocess.run(
            ["lsof", "-t", f"-iTCP:{port}", "-sTCP:LISTEN"],
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        return
    for pid_s in r.stdout.strip().split():
        if not pid_s.isdigit():
            continue
        try:
            pid = int(pid_s)
            _emit(f"[smoke nuke] SIGTERM listener PID {pid} :{port}", flush=True)
            os.kill(pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass


# Extra ports often left bound after interrupted smoke/serve cycles (Windows SO_REUSEADDR footguns).
_SMOKE_EXTRA_LOOPBACK_PORTS = (4204, 4205, 4206, 4207, 4208, 11152)


def _nuke_stale_browsers_for_smoke() -> None:
    """Close agent-browser and other headless Chrome left from ce-test-browser runs.

    Concurrent SwiftShader/ANGLE WebGL contexts on one host can make SDL_GL_CreateContext
    fail in Playwright smoke (``Could not create webgl context``).
    """
    _emit("[smoke nuke] closing stale agent-browser / headless Chrome …", flush=True)
    try:
        subprocess.run(
            ["agent-browser", "close"],
            capture_output=True,
            text=True,
            timeout=15,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        pass
    if sys.platform == "win32":
        subprocess.run(
            [
                "taskkill",
                "/F",
                "/IM",
                "chrome.exe",
                "/FI",
                "WINDOWTITLE eq agent-browser*",
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
    else:
        subprocess.run(
            ["pkill", "-f", "agent-browser-chrome"],
            capture_output=True,
            text=True,
            timeout=15,
        )
    time.sleep(0.5)


def _nuke_stale_http_for_smoke(url: str) -> None:
    """Kill leftover ``serve.py`` and anything LISTENING on the loopback port implied by ``url``."""
    _nuke_stale_browsers_for_smoke()
    _emit("[smoke nuke] killing stale tools/web/serve.py and localhost listeners …", flush=True)
    if sys.platform == "win32":
        _kill_windows_tools_web_serve_py()
    else:
        _kill_unix_tools_web_serve_py()
    time.sleep(0.4)
    lp = _localhost_listen_port_from_url(url)
    ports_to_clear = set(_SMOKE_EXTRA_LOOPBACK_PORTS)
    if lp is not None:
        ports_to_clear.add(lp)
    for port in sorted(ports_to_clear):
        if sys.platform == "win32":
            _kill_windows_listen_port(port)
        else:
            _kill_unix_listen_port(port)
    time.sleep(0.3)


def _terminate_server_proc(proc: subprocess.Popen | None) -> None:
    """Stop ``serve.py`` subprocess and any child worker threads (Windows needs /T)."""
    if proc is None:
        return
    pid = proc.pid
    if sys.platform == "win32":
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(pid)],
            capture_output=True,
            text=True,
            timeout=60,
        )
        return
    try:
        proc.terminate()
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass


def _resolve_web_bin(web_bin: pathlib.Path) -> pathlib.Path:
    """If CMake wrote reone-web-output-dir.txt (Windows wasm output redirect), use that directory."""
    return resolve_web_build_directory(web_bin.expanduser().resolve())


def _preflight_local_wasm(web_bin: pathlib.Path) -> int:
    """Catch truncated engine.wasm (e.g. wasm-ld 'permission denied' on Windows leaves a 0-byte file)."""
    wasm = web_bin / "engine.wasm"
    if not wasm.is_file():
        return 0
    try:
        n = wasm.stat().st_size
    except OSError as e:
        _emit(f"Smoke: cannot stat {wasm}: {e}", file=sys.stderr)
        return 4
    if n < 32768:
        _emit(
            f"Smoke: {wasm} is only {n} bytes — wasm link likely failed or file is locked.\n"
            "Close any browser tab, Playwright, or serve.py using this tree; delete engine.wasm; "
            "then rebuild: cmake --build build-web --target engine",
            file=sys.stderr,
        )
        return 4
    try:
        with wasm.open("rb") as f:
            head = f.read(8)
        if not head.startswith(b"\x00asm"):
            _emit(
                f"Smoke: {wasm} missing wasm magic (got {head[:4]!r}) — rebuild target engine",
                file=sys.stderr,
            )
            return 4
    except OSError as e:
        _emit(f"Smoke: cannot read wasm header {wasm}: {e}", file=sys.stderr)
        return 4
    return 0


def _preflight_http(url: str) -> int:
    """Return 0 if ``url`` responds with HTTP; 4 on connection/empty response; 3 if HTTP mirror has no files."""
    try:
        urllib.request.urlopen(url, timeout=15)
    except urllib.error.HTTPError as e:
        if e.code in (301, 302, 303, 307, 308):
            return 0
        _emit(f"Preflight GET {url!r} failed: HTTP {e.code}", file=sys.stderr)
        return 4
    except urllib.error.URLError as e:
        _emit(
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
                _emit(
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
            _emit(
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
        cwd=str(repo_root),
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
    ap.add_argument(
        "--warp",
        default=None,
        help=(
            "Module resref to load after the menu is ready (e.g. end_m01aa). Verifies the area "
            "renders in-game. Also reads REONE_WEB_SMOKE_WARP_MODULE."
        ),
    )
    ap.add_argument("--timeout", type=float, default=1800.0, help="seconds (default 30 min)")
    ap.add_argument("--interval", type=float, default=10.0)
    ap.add_argument(
        "--spawn-serve",
        action="store_true",
        help="Start tools/web/serve.py on a free port (needs --game-root)",
    )
    ap.add_argument(
        "--no-nuke-stale-servers",
        action="store_true",
        help="Do not kill leftover serve.py / listeners before running (default: nuke ON)",
    )
    ap.add_argument(
        "--game-root",
        type=pathlib.Path,
        default=None,
        help="Retail KotOR dir (chitin.key required; swkotor.exe optional); also reads REONE_WEB_SMOKE_GAME_ROOT",
    )
    ap.add_argument(
        "--web-bin",
        type=pathlib.Path,
        default=pathlib.Path("build-web/bin"),
        help="Directory containing engine.html",
    )
    args = ap.parse_args()

    if not args.no_nuke_stale_servers:
        _nuke_stale_http_for_smoke(args.url)

    url = args.url
    proc: subprocess.Popen | None = None
    game_root = args.game_root or (
        pathlib.Path(os.environ["REONE_WEB_SMOKE_GAME_ROOT"])
        if os.environ.get("REONE_WEB_SMOKE_GAME_ROOT")
        else None
    )

    web_bin_resolved = _resolve_web_bin(args.web_bin)

    if args.spawn_serve:
        if not game_root or not game_root.is_dir():
            _emit(
                "Smoke --spawn-serve requires --game-root or REONE_WEB_SMOKE_GAME_ROOT "
                "pointing at a retail KotOR install (chitin.key on disk).",
                file=sys.stderr,
            )
            return 2
        if not _is_kotor_root(game_root.resolve()):
            _emit(
                f"Game root does not look like retail KotOR 1: {game_root}\n"
                "Need chitin.key >= 64 KiB and dialog.tlk TLK V3.0 (swkotor.exe optional).\n"
                "Set REONE_WEB_SMOKE_GAME_ROOT to your install, e.g. SteamLibrary/.../swkotor.",
                file=sys.stderr,
            )
            return 2
        wchk = _preflight_local_wasm(web_bin_resolved)
        if wchk != 0:
            return wchk
        port = _free_port()
        proc = _spawn_local_mirror(web_bin_resolved, game_root.resolve(), port)
        time.sleep(2.0)
        if proc.poll() is not None:
            _emit("serve.py exited early (check --web-bin and port).", file=sys.stderr)
            return 2
        url = f"http://127.0.0.1:{port}/engine.html"
        _emit(f"Serving wasm from {web_bin_resolved} with mirror {game_root} -> {url}", flush=True)
    else:
        wchk = _preflight_local_wasm(web_bin_resolved)
        if wchk != 0:
            return wchk

    pre = _preflight_http(url)
    if pre != 0:
        return pre

    warp_module = args.warp or os.environ.get("REONE_WEB_SMOKE_WARP_MODULE") or None
    if warp_module:
        warp_module = warp_module.strip().lower() or None

    try:
        return asyncio.run(run(url, args.out, args.timeout, args.interval, warp_module))
    finally:
        _terminate_server_proc(proc)


if __name__ == "__main__":
    raise SystemExit(main())
