from __future__ import annotations

import argparse
import functools
import http.server
import mimetypes
import pathlib
import socket
import socketserver


class WebBuildRequestHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def copyfile(self, source, outputfile):
        # Stream large bundles (engine.data can be several GB) without relying on
        # shutil.copyfileobj internals that have shown MemoryError in this setup.
        chunk_size = 256 * 1024
        buffer = bytearray(chunk_size)
        view = memoryview(buffer)
        try:
            while True:
                read_bytes = source.readinto(buffer)
                if not read_bytes:
                    break
                outputfile.write(view[:read_bytes])
        except (BrokenPipeError, ConnectionResetError, socket.timeout):
            # Browser canceled or disconnected mid-transfer.
            return


class ReusableThreadingTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser(description="Serve the reone WebAssembly build")
    parser.add_argument("--directory", default="build-web/bin", help="directory containing engine.html")
    parser.add_argument("--port", type=int, default=4024, help="localhost port")
    args = parser.parse_args()

    mimetypes.add_type("application/wasm", ".wasm")
    directory = pathlib.Path(args.directory).resolve()
    handler = functools.partial(WebBuildRequestHandler, directory=str(directory))

    with ReusableThreadingTCPServer(("localhost", args.port), handler) as server:
        print(f"Serving {directory} at http://localhost:{args.port}", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
