"""Quick smoke test for serve.py (manifest + Range). Run from this directory: python test_serve_smoke.py"""

import pathlib
import tempfile
import threading
import urllib.request

import serve as serve_mod


def main():
    td = pathlib.Path(tempfile.mkdtemp())
    (td / "engine.html").write_text("<html></html>", encoding="utf-8")
    (td / "game-manifest.json").write_text('{"files":["a.bin"]}', encoding="utf-8")
    (td / "a.bin").write_bytes(b"0123456789")

    factory = serve_mod._handler_factory(td, td)
    srv = serve_mod.ReusableThreadingTCPServer(("localhost", 0), factory)
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()

    base = f"http://localhost:{port}"
    assert urllib.request.urlopen(base + "/game-manifest.json").read().startswith(b"{")
    req = urllib.request.Request(base + "/game-files/a.bin", headers={"Range": "bytes=2-5"})
    resp = urllib.request.urlopen(req)
    assert resp.status == 206
    assert resp.read() == b"2345"
    srv.shutdown()
    print("serve tests ok")


if __name__ == "__main__":
    main()
