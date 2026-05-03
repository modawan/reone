// browser game dir: full install under /game via the File System Access API (chitin.key at root).
// Bundled via Emscripten --pre-js with the WASM module.
//
// Keep game data out of the wasm bundle: index the selected folder into lightweight /game marker files,
// then let WebFileInputStream read exact byte ranges from FileSystemFileHandle/Blob.slice on demand.
// Optional skips reduce startup indexing work (movies/music/saves); set Module.reoneWebFsNoSkip = true to index all folders.
//
// Dev/automation: if same-origin GET /game-manifest.json succeeds (serve.py --game-root), we index listed files and
// read /game-files/... with Range requests on demand — no game data in git or embedded wasm artifacts.
(function () {
    if (typeof Module === "undefined") {
        Module = {};
    }

    var previousPreRun = Module.preRun || [];
    if (typeof previousPreRun === "function") {
        previousPreRun = [previousPreRun];
    }
    Module.preRun = previousPreRun;

    function ensureDirectory(path) {
        var parts = path.split("/").filter(Boolean);
        var current = "";
        for (var i = 0; i < parts.length; ++i) {
            current += "/" + parts[i];
            try {
                FS.mkdir(current);
            } catch (e) {
                // Already exists.
            }
        }
    }

    function truthyEmbOpt(v) {
        return v === true || v === 1 || v === "1";
    }

    /** Non-parity: CMake embedded full/custom preload already populated /game; skip FS Access when opted in. */
    function skipFsEmbeddedPreload() {
        if (!truthyEmbOpt(Module.reoneAssumeEmbeddedGamePreload)) {
            return false;
        }
        // Do not FS.stat("/game/...") here: MEMFS from --preload-file may not be visible yet during preRun,
        // which would deadlock runDependencies waiting on the folder picker. C++ main checks /game for swkotor*.exe.
        return true;
    }

    var IDB_NAME = "reone-web-game";
    var IDB_STORE = "fs-access";
    var IDB_KEY = "directory-handle";

    function idbOpen() {
        return new Promise(function (resolve, reject) {
            var req = indexedDB.open(IDB_NAME, 1);
            req.onerror = function () {
                reject(req.error);
            };
            req.onupgradeneeded = function () {
                req.result.createObjectStore(IDB_STORE);
            };
            req.onsuccess = function () {
                resolve(req.result);
            };
        });
    }

    function idbGetHandle() {
        return idbOpen().then(function (db) {
            return new Promise(function (resolve, reject) {
                var tx = db.transaction(IDB_STORE, "readonly");
                var rq = tx.objectStore(IDB_STORE).get(IDB_KEY);
                rq.onsuccess = function () {
                    resolve(rq.result);
                };
                rq.onerror = function () {
                    reject(rq.error);
                };
            });
        });
    }

    function idbPutHandle(handle) {
        return idbOpen().then(function (db) {
            return new Promise(function (resolve, reject) {
                var tx = db.transaction(IDB_STORE, "readwrite");
                tx.objectStore(IDB_STORE).put(handle, IDB_KEY);
                tx.oncomplete = function () {
                    resolve();
                };
                tx.onerror = function () {
                    reject(tx.error);
                };
            });
        });
    }

    function idbDeleteHandle() {
        return idbOpen().then(function (db) {
            return new Promise(function (resolve, reject) {
                var tx = db.transaction(IDB_STORE, "readwrite");
                tx.objectStore(IDB_STORE).delete(IDB_KEY);
                tx.oncomplete = function () {
                    resolve();
                };
                tx.onerror = function () {
                    reject(tx.error);
                };
            });
        });
    }

    async function validateDirectoryHandleRW(handle) {
        try {
            var q = await handle.queryPermission({ mode: "readwrite" });
            if (q === "granted") {
                return true;
            }
            var r = await handle.requestPermission({ mode: "readwrite" });
            return r === "granted";
        } catch (e) {
            console.warn("reone web: directory permission", e);
            return false;
        }
    }

    async function findRootEntryInsensitive(dirHandle, wantName, kind) {
        var low = wantName.toLowerCase();
        var entries = [];
        for await (var entry of dirHandle.values()) {
            entries.push(entry);
        }
        for (var i = 0; i < entries.length; ++i) {
            var entry = entries[i];
            if (entry.kind !== kind) {
                continue;
            }
            if (entry.name.toLowerCase() === low) {
                return entry;
            }
        }
        return null;
    }

    async function ensureChitinKey(dirHandle) {
        var fh = await findRootEntryInsensitive(dirHandle, "chitin.key", "file");
        return !!fh;
    }

    function injectGateUi() {
        if (document.getElementById("reone-fs-gate")) {
            return;
        }
        var style = document.createElement("style");
        style.textContent =
            "#reone-fs-gate{position:fixed;inset:0;z-index:2147483647;display:flex;align-items:center;justify-content:center;background:#111827;color:#e5e7eb;font-family:system-ui,sans-serif;padding:24px;box-sizing:border-box}" +
            "#reone-fs-gate-inner{max-width:420px;text-align:center}" +
            "#reone-fs-gate h2{font-size:1.125rem;margin:0 0 12px;font-weight:600}" +
            "#reone-fs-gate p{font-size:0.875rem;line-height:1.5;margin:0 0 16px;color:#9ca3af}" +
            "#reone-fs-gate button{font:inherit;padding:10px 18px;border-radius:8px;border:none;background:#2563eb;color:#fff;cursor:pointer}" +
            "#reone-fs-gate button:hover{background:#1d4ed8}" +
            "#reone-fs-gate button:focus-visible{outline:2px solid #93c5fd;outline-offset:2px}" +
            "#reone-fs-gate .err{color:#fca5a5;margin-top:12px;font-size:0.8125rem}";
        document.head.appendChild(style);
        var root = document.createElement("div");
        root.id = "reone-fs-gate";
        root.innerHTML =
            '<div id="reone-fs-gate-inner"><h2>Select KotOR installation</h2>' +
            '<p>Choose the folder that contains <strong>chitin.key</strong>. Files stay on your device.</p>' +
            '<button type="button" id="reone-fs-gate-btn">Choose folder…</button>' +
            '<p class="err" id="reone-fs-gate-err" hidden></p></div>';
        document.body.appendChild(root);
    }

    function removeGateUi() {
        var g = document.getElementById("reone-fs-gate");
        if (g) {
            g.remove();
        }
    }

    function showGateError(msg) {
        var el = document.getElementById("reone-fs-gate-err");
        if (!el) {
            return;
        }
        el.hidden = false;
        el.textContent = msg;
    }

    function waitForUserDirectoryPick() {
        injectGateUi();
        return new Promise(function (resolve) {
            var btn = document.getElementById("reone-fs-gate-btn");
            if (!btn) {
                resolve(null);
                return;
            }
            btn.onclick = function () {
                var errEl = document.getElementById("reone-fs-gate-err");
                if (errEl) {
                    errEl.hidden = true;
                    errEl.textContent = "";
                }
                if (!window.showDirectoryPicker) {
                    showGateError("This browser does not support folder selection (File System Access API).");
                    resolve(null);
                    return;
                }
                window
                    .showDirectoryPicker({ mode: "readwrite", id: "reone-kotor-install" })
                    .then(resolve)
                    .catch(function (e) {
                        if (e && e.name === "AbortError") {
                            resolve(null);
                            return;
                        }
                        showGateError(e && e.message ? e.message : String(e));
                        resolve(null);
                    });
            };
        });
    }

    function defaultSkipTopDirs() {
        return ["movies", "streammusic", "saves"];
    }

    function shouldSkipSubtree(relPosixLower) {
        if (truthyEmbOpt(Module.reoneWebFsNoSkip)) {
            return false;
        }
        var skips = Module.reoneWebFsSkipTopDirs;
        if (!Array.isArray(skips)) {
            skips = defaultSkipTopDirs();
        }
        var seg0 = relPosixLower.split("/").filter(Boolean)[0];
        if (!seg0) {
            return false;
        }
        for (var s = 0; s < skips.length; ++s) {
            if (seg0 === String(skips[s]).toLowerCase()) {
                return true;
            }
        }
        return false;
    }

    /**
     * serve.py HTTP mirror: extra top-level folders to skip so dev/CI reaches the engine quickly.
     * streamwaves is often 10k+ tiny WAVs; Override is frequently thousands of mods — neither is required
     * to boot to the main menu shell. streamsounds is smaller but still omit-for-speed unless opted in.
     */
    function shouldSkipHttpMirrorSubtree(relPosixLower) {
        if (shouldSkipSubtree(relPosixLower)) {
            return true;
        }
        if (truthyEmbOpt(Module.reoneWebFsNoSkip)) {
            return false;
        }
        if (Array.isArray(Module.reoneWebFsMirrorExtraSkipTopDirs)) {
            var seg = relPosixLower.split("/").filter(Boolean)[0];
            if (!seg) {
                return false;
            }
            var extra = Module.reoneWebFsMirrorExtraSkipTopDirs;
            for (var i = 0; i < extra.length; ++i) {
                if (seg === String(extra[i]).toLowerCase()) {
                    return true;
                }
            }
            return false;
        }
        var seg0 = relPosixLower.split("/").filter(Boolean)[0];
        if (!seg0) {
            return false;
        }
        var defaults = ["streamwaves", "override", "streamsounds"];
        for (var j = 0; j < defaults.length; ++j) {
            if (seg0 === defaults[j]) {
                return true;
            }
        }
        return false;
    }

    function httpGameFileUrl(relPosix) {
        var parts = relPosix.split("/").filter(Boolean);
        return "/game-files/" + parts.map(encodeURIComponent).join("/");
    }

    function normalizeGamePath(path) {
        var p = String(path || "").replace(/\\/g, "/").toLowerCase();
        p = p.replace(/^\/+/, "");
        if (p === "game") {
            return "";
        }
        if (p.indexOf("game/") === 0) {
            p = p.slice(5);
        }
        return p.replace(/^\/+/, "");
    }

    function ensureEmptyFile(path) {
        try {
            FS.stat(path);
            return;
        } catch (e) {
            // Create marker below.
        }
        var parentFs = path.slice(0, path.lastIndexOf("/"));
        ensureDirectory(parentFs);
        FS.writeFile(path, new Uint8Array(0));
    }

    function parseContentLengthFromHeaders(headers, fallbackLength) {
        var contentRange = headers.get("Content-Range") || "";
        var slash = contentRange.lastIndexOf("/");
        if (slash !== -1) {
            var total = Number(contentRange.slice(slash + 1));
            if (Number.isFinite(total)) {
                return total;
            }
        }
        var len = Number(headers.get("Content-Length") || fallbackLength || "-1");
        return Number.isFinite(len) ? len : -1;
    }

    async function fetchMirrorRange(relPosix, rangeStart, rangeLen) {
        var url = httpGameFileUrl(relPosix);
        var headers = {};
        if (typeof rangeStart === "number") {
            var end = typeof rangeLen === "number" && rangeLen > 0 ? rangeStart + rangeLen - 1 : rangeStart;
            headers.Range = "bytes=" + rangeStart + "-" + end;
        }
        var resp = await fetch(url, { credentials: "same-origin", headers: headers });
        if (resp.status === 416) {
            return { bytes: new Uint8Array(0), total: -1 };
        }
        if (!resp.ok && resp.status !== 206) {
            throw new Error("reone web: " + url + " -> HTTP " + resp.status);
        }
        var buf = await resp.arrayBuffer();
        return {
            bytes: new Uint8Array(buf),
            total: parseContentLengthFromHeaders(resp.headers, buf.byteLength),
        };
    }

    Module.reoneWebFileLengthAsync = async function (path) {
        var rel = normalizeGamePath(path);
        var handleFiles = Module.reoneWebHandleFiles || {};
        var handle = handleFiles[rel];
        if (handle) {
            try {
                return (await handle.getFile()).size;
            } catch (e) {
                console.error("reone web: File System Access stat failed:", rel, e);
                return -1;
            }
        }
        var lookup = Module.reoneWebHttpMirrorFiles || {};
        var original = lookup[rel];
        if (!original) return -1;
        try {
            return (await fetchMirrorRange(original, 0, 1)).total;
        } catch (e) {
            console.error("reone web: stat failed:", rel, e);
            return -1;
        }
    };

    Module.reoneWebFileReadAsync = async function (path, offset, len) {
        var rel = normalizeGamePath(path);
        if (len <= 0) {
            return new Uint8Array(0);
        }
        var handleFiles = Module.reoneWebHandleFiles || {};
        var handle = handleFiles[rel];
        if (handle) {
            try {
                var file = await handle.getFile();
                return new Uint8Array(await file.slice(offset, offset + len).arrayBuffer());
            } catch (e) {
                console.error("reone web: File System Access range read failed:", rel, offset, len, e);
                return null;
            }
        }
        var lookup = Module.reoneWebHttpMirrorFiles || {};
        var original = lookup[rel];
        if (!original) return null;
        try {
            return (await fetchMirrorRange(original, offset, len)).bytes;
        } catch (e) {
            console.error("reone web: range read failed:", rel, offset, len, e);
            return null;
        }
    };

    /**
     * Expose /game from tools/web/serve.py --game-root (game-manifest.json + ranged /game-files/...).
     */
    async function tryMountFromHttpMirror() {
        if (truthyEmbOpt(Module.reoneWebFsDisableHttpMirror)) {
            return false;
        }
        var manifestUrl =
            typeof Module.reoneWebGameManifestUrl === "string"
                ? Module.reoneWebGameManifestUrl
                : "/game-manifest.json";
        var man;
        try {
            var resp = await fetch(manifestUrl, { credentials: "same-origin" });
            if (!resp.ok) {
                return false;
            }
            man = await resp.json();
            if (!man || !Array.isArray(man.files)) {
                return false;
            }
        } catch (e) {
            return false;
        }

        ensureDirectory("/game");
        transitionGateToLoadingUi();
        setGateLoadingMessage("Indexing game files from local server mirror…");

        var dirs = Array.isArray(man.directories) ? man.directories : [];
        for (var d = 0; d < dirs.length; ++d) {
            var relDir = String(dirs[d]).replace(/\\/g, "/");
            var lowDir = relDir.toLowerCase();
            if (shouldSkipHttpMirrorSubtree(lowDir)) {
                continue;
            }
            ensureDirectory("/game/" + lowDir);
        }

        var files = man.files;
        var lookup = {};
        var indexed = 0;
        for (var fi = 0; fi < files.length; ++fi) {
            var relF = String(files[fi]).replace(/\\/g, "/");
            var lowF = relF.toLowerCase();
            if (shouldSkipHttpMirrorSubtree(lowF)) {
                continue;
            }
            lookup[lowF] = relF;
            ensureEmptyFile("/game/" + lowF);
            indexed++;
            if ((indexed & 255) === 0) {
                setGateLoadingMessage(
                    "Indexed " +
                        indexed +
                        " lazy game files from server mirror… (no archive bytes copied)"
                );
                await new Promise(function (r) {
                    setTimeout(r, 0);
                });
            }
        }

        Module.reoneWebHttpMirrorFiles = lookup;
        Module.reoneWebLazyGameFsActive = true;
        setGateLoadingMessage("Indexed " + indexed + " lazy game files. Starting engine…");
        return true;
    }

    function transitionGateToLoadingUi() {
        var inner = document.getElementById("reone-fs-gate-inner");
        if (!inner) {
            injectGateUi();
            inner = document.getElementById("reone-fs-gate-inner");
        }
        inner.innerHTML =
            '<h2>Loading game files</h2>' +
            '<p class="reone-fs-load-detail" style="font-size:0.8125rem;color:#9ca3af;margin-top:12px;line-height:1.5"></p>';
    }

    function setGateLoadingMessage(line) {
        var detail = document.querySelector(".reone-fs-load-detail");
        if (detail) {
            detail.textContent = line;
        }
        if (typeof Module === "object" && Module.setStatus) {
            Module.setStatus(line);
        }
    }

    async function mountFromDirectoryHandle(rootHandle) {
        ensureDirectory("/game");
        var fileCount = 0;
        var skippedTrees = 0;
        var handleFiles = {};

        async function walk(dirHandle, relPrefix) {
            var entries = [];
            for await (var entry of dirHandle.values()) {
                entries.push(entry);
            }
            for (var i = 0; i < entries.length; ++i) {
                var entry = entries[i];
                var rel = relPrefix ? relPrefix + "/" + entry.name : entry.name;
                var norm = rel.replace(/\\/g, "/");
                var normLower = norm.toLowerCase();

                if (entry.kind === "directory") {
                    if (shouldSkipSubtree(normLower)) {
                        skippedTrees++;
                        continue;
                    }
                    await walk(entry, norm);
                    continue;
                }

                if (entry.kind !== "file") {
                    continue;
                }

                var fsRel = normLower;
                var fsPath = "/game/" + fsRel;
                handleFiles[fsRel] = entry;
                ensureEmptyFile(fsPath);
                fileCount++;
                if ((fileCount & 63) === 0) {
                    setGateLoadingMessage(
                        "Indexed " +
                            fileCount +
                            " lazy files from your KotOR install…" +
                            (skippedTrees ? " (" + skippedTrees + " heavy folders skipped)" : "")
                    );
                    await new Promise(function (r) {
                        setTimeout(r, 0);
                    });
                }
            }
        }

        transitionGateToLoadingUi();
        setGateLoadingMessage(
            "Indexing your KotOR install for on-demand reads. " +
                "movies/, streammusic/, and saves/ are skipped by default — set Module.reoneWebFsNoSkip=true before engine.js to index everything."
        );

        await walk(rootHandle, "");
        Module.reoneWebHandleFiles = handleFiles;
        Module.reoneWebLazyGameFsActive = true;
        setGateLoadingMessage("Finished indexing " + fileCount + " lazy files. Starting engine…");
    }

    Module.preRun.push(function () {
        var dependency = "reone-game-fs";
        addRunDependency(dependency);

        function done() {
            removeRunDependency(dependency);
        }

        Promise.resolve()
            .then(async function () {
                if (skipFsEmbeddedPreload()) {
                    done();
                    return;
                }

                try {
                    if (await tryMountFromHttpMirror()) {
                        removeGateUi();
                        done();
                        return;
                    }
                } catch (e) {
                    console.error("reone web: HTTP game mirror failed:", e);
                    injectGateUi();
                    showGateError(
                        (e && e.message ? String(e.message) + " — " : "") +
                            "Pick your KotOR install folder below, or fix serve.py --game-root + game-manifest.json."
                    );
                }

                if (typeof window.showDirectoryPicker !== "function") {
                    injectGateUi();
                    showGateError(
                        "No game mirror found at /game-manifest.json and this browser does not support folder selection (File System Access). Use Chrome or Edge with a local KotOR folder, or run: python tools/web/serve.py --directory …/bin --game-root \"…/Your KotOR\" (see doc/webassembly.md)."
                    );
                    done();
                    return;
                }

                var rootHandle = Module.reoneGameDirectoryHandle;
                if (!rootHandle) {
                    try {
                        rootHandle = await idbGetHandle();
                    } catch (e) {
                        rootHandle = undefined;
                    }
                }

                if (rootHandle) {
                    if (!(await validateDirectoryHandleRW(rootHandle))) {
                        await idbDeleteHandle().catch(function () {});
                        rootHandle = undefined;
                    } else if (!(await ensureChitinKey(rootHandle))) {
                        await idbDeleteHandle().catch(function () {});
                        rootHandle = undefined;
                    }
                }

                while (!rootHandle) {
                    var picked = await waitForUserDirectoryPick();
                    if (!picked) {
                        continue;
                    }
                    if (!(await validateDirectoryHandleRW(picked))) {
                        showGateError("Read/write permission to the folder was denied.");
                        continue;
                    }
                    if (!(await ensureChitinKey(picked))) {
                        showGateError("Could not find chitin.key in that folder.");
                        continue;
                    }
                    rootHandle = picked;
                    await idbPutHandle(rootHandle);
                }

                if (!Module.reoneGameDirectoryHandle) {
                    Module.reoneGameDirectoryHandle = rootHandle;
                }

                await mountFromDirectoryHandle(rootHandle);
                removeGateUi();
                done();
            })
            .catch(function (err) {
                console.error("reone web: game filesystem setup failed:", err);
                done();
            });
    });
})();
