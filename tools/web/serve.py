from __future__ import annotations

import argparse
import http.server
import mimetypes
import pathlib
import socket
import socketserver
import urllib.parse


def _parse_bytes_range(range_header: str | None, file_size: int) -> tuple[int, int] | None:
    """Return inclusive (start, end) for a single Range spec, or None for full body."""
    if not range_header or not range_header.strip().lower().startswith("bytes="):
        return None
    spec = range_header.split("=", 1)[1].strip()
    if "," in spec:
        spec = spec.split(",", 1)[0].strip()
    spec = spec.strip()
    try:
        if spec.startswith("-"):
            suffix_len = int(spec[1:])
            if suffix_len <= 0:
                return None
            start = max(0, file_size - suffix_len)
            end = file_size - 1
            return start, end
        if "-" in spec:
            start_s, end_s = spec.split("-", 1)
            start = int(start_s) if start_s.strip() else 0
            end = int(end_s) if end_s.strip() else file_size - 1
        else:
            return None
    except ValueError:
        return None
    end = min(end, file_size - 1)
    if start < 0 or start >= file_size or start > end:
        return None
    return start, end


class WebBuildRequestHandler(http.server.SimpleHTTPRequestHandler):
    """Serve WASM build from ``directory`` and optional lazy game files from ``game_root``."""

    def __init__(self, *args, game_root: pathlib.Path | None = None, **kwargs):
        self._lazy_game_root = game_root.resolve() if game_root else None
        super().__init__(*args, **kwargs)

    def end_headers(self):
        request_path = self.path.split("?", 1)[0]
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        if request_path.endswith((".data", ".wasm", ".js")):
            self.send_header("Cache-Control", "public, max-age=0, must-revalidate")
        else:
            self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def copyfile(self, source, outputfile):
        chunk_size = 256 * 1024
        buffer = bytearray(chunk_size)
        view = memoryview(buffer)
        range_tuple = getattr(self, "range", None)
        remaining = None
        if isinstance(range_tuple, tuple) and len(range_tuple) == 2:
            start, end = range_tuple
            if isinstance(start, int) and isinstance(end, int) and end >= start:
                remaining = end - start + 1
        try:
            while True:
                if remaining is not None:
                    if remaining <= 0:
                        break
                    read_bytes = source.readinto(view[: min(chunk_size, remaining)])
                else:
                    read_bytes = source.readinto(buffer)
                if not read_bytes:
                    break
                outputfile.write(view[:read_bytes])
                if remaining is not None:
                    remaining -= read_bytes
        except (BrokenPipeError, ConnectionResetError, socket.timeout):
            return

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path_only = urllib.parse.unquote(parsed.path)
        if path_only == "/game-manifest.json":
            self._serve_game_manifest()
            return
        if path_only.startswith("/game-files/"):
            rel = path_only[len("/game-files/") :]
            self._serve_lazy_game_file(rel)
            return
        super().do_GET()

    def _resolve_under_game_root(self, relative_url_path: str) -> pathlib.Path | None:
        if not self._lazy_game_root:
            return None
        rel = urllib.parse.unquote(relative_url_path).replace("\\", "/").lstrip("/")
        if not rel or rel.endswith("/"):
            return None
        parts = pathlib.PurePosixPath(rel).parts
        if ".." in parts:
            return None
        full = (self._lazy_game_root / pathlib.Path(*parts)).resolve()
        try:
            full.relative_to(self._lazy_game_root)
        except ValueError:
            return None
        return full

    def _serve_game_manifest(self):
        if not self._lazy_game_root:
            self.send_error(503, "Set --game-root to serve /game-manifest.json")
            return
        manifest_path = self._lazy_game_root / "game-manifest.json"
        if not manifest_path.is_file():
            self.send_error(
                404,
                "game-manifest.json not found under game root — run tools/web/gen_game_manifest.py",
            )
            return
        self._send_local_file(manifest_path)

    def _serve_lazy_game_file(self, relative_url_path: str):
        full_path = self._resolve_under_game_root(relative_url_path)
        if full_path is None:
            self.send_error(403 if self._lazy_game_root else 503)
            return
        if not full_path.is_file():
            self.send_error(404)
            return
        self._send_local_file(full_path)

    def _send_local_file(self, full_path: pathlib.Path):
        file_size = full_path.stat().st_size
        ctype = mimetypes.guess_type(str(full_path))[0] or "application/octet-stream"
        range_pair = _parse_bytes_range(self.headers.get("Range"), file_size)

        try:
            f = open(full_path, "rb")
        except OSError:
            self.send_error(403)
            return

        with f:
            if range_pair is None:
                self.send_response(http.HTTPStatus.OK)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(file_size))
                self.send_header("Accept-Ranges", "bytes")
                self.end_headers()
                self.copyfile(f, self.wfile)
                return

            start, end = range_pair
            chunk_len = end - start + 1
            self.send_response(http.HTTPStatus.PARTIAL_CONTENT)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(chunk_len))
            self.send_header("Content-Range", f"bytes {start}-{end}/{file_size}")
            self.send_header("Accept-Ranges", "bytes")
            self.end_headers()
            f.seek(start)
            remaining = chunk_len
            buf_size = 256 * 1024
            while remaining > 0:
                n = min(buf_size, remaining)
                data = f.read(n)
                if not data:
                    break
                self.wfile.write(data)
                remaining -= len(data)


class ReusableThreadingTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class ReusableDualStackThreadingTCPServer(socketserver.ThreadingTCPServer):
    """Listen on IPv6 ``::`` with ``IPV6_V6ONLY=0`` so ``localhost`` (often ::1) and IPv4 clients both work."""

    allow_reuse_address = True
    daemon_threads = True
    address_family = socket.AF_INET6

    def server_bind(self) -> None:
        self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
        super().server_bind()


def _handler_factory(web_directory: pathlib.Path, game_root: pathlib.Path | None):
    wd = web_directory.resolve()
    gr = game_root.resolve() if game_root else None

    def _factory(request, client_address, server):
        return WebBuildRequestHandler(request, client_address, server, directory=str(wd), game_root=gr)

    return _factory


def main():
    parser = argparse.ArgumentParser(description="Serve the reone WebAssembly build")
    parser.add_argument("--directory", default="build-web/bin", help="directory containing engine.html")
    parser.add_argument(
        "--game-root",
        default=None,
        help="KotOR install directory (serves /game-manifest.json and /game-files/... for lazy loading)",
    )
    parser.add_argument("--port", type=int, default=4204, help="localhost port")
    args = parser.parse_args()

    mimetypes.add_type("application/wasm", ".wasm")
    mimetypes.add_type("application/json", ".json")
    directory = pathlib.Path(args.directory).resolve()
    game_root = pathlib.Path(args.game_root).resolve() if args.game_root else None
    handler_factory = _handler_factory(directory, game_root)

    listen_dual_stack = True
    try:
        server_cm = ReusableDualStackThreadingTCPServer(("::", args.port), handler_factory)
    except OSError:
        listen_dual_stack = False
        server_cm = ReusableThreadingTCPServer(("127.0.0.1", args.port), handler_factory)

    with server_cm as server:
        extra = f" game-root={game_root}" if game_root else ""
        print(f"Serving {directory} at http://127.0.0.1:{args.port}{extra}", flush=True)
        if listen_dual_stack:
            print(f"Also reachable at http://localhost:{args.port}{extra} (dual-stack IPv6/IPv4)", flush=True)
        else:
            print(
                f"If http://localhost:{args.port} fails, use http://127.0.0.1:{args.port} (IPv6 bind unavailable).",
                flush=True,
            )
        server.serve_forever()


if __name__ == "__main__":
    main()
