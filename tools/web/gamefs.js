// KobaltBlu/KotOR.js-style browser game dir: full install under /game via the File System Access API (chitin.key at root).
// Bundled via Emscripten --pre-js with the WASM module (same class of setup as KotOR.js glue, not a separate shell script).
//
// Emscripten FS.createLazyFile uses synchronous XHR / worker-only paths and aborts on the browser main thread, so we copy
// each picked file into MEMFS with FS.writeFile after file.arrayBuffer(). Optional skips reduce RAM/time (movies/music/saves).
// Set Module.reoneWebFsNoSkip = true before engine.js for a full tree like KotOR.js exposes (needs enough browser RAM).
//
// Dev/automation: if same-origin GET /game-manifest.json succeeds (serve.py --game-root), we fetch listed files from
// /game-files/... into MEMFS before wasm starts — no game data in git; mirror is read from the user's local install path.
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
            '<p>Choose the folder that contains <strong>chitin.key</strong> (same layout as KotOR.js). Files stay on your device.</p>' +
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

    /**
     * Load /game from tools/web/serve.py --game-root (game-manifest.json + ranged /game-files/...).
     * KotOR.js parity for shipping remains FS Access; this path is for local dev/CI only.
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
        setGateLoadingMessage("Loading game files from local server mirror…");

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
        var filtered = [];
        for (var fi = 0; fi < files.length; ++fi) {
            var relF = String(files[fi]).replace(/\\/g, "/");
            if (!shouldSkipHttpMirrorSubtree(relF.toLowerCase())) {
                filtered.push(relF);
            }
        }

        var loaded = 0;
        var totalListed = files.length;
        var mirrorConcurrency = 8;
        if (typeof Module.reoneWebFsMirrorConcurrency === "number" && Module.reoneWebFsMirrorConcurrency > 0) {
            mirrorConcurrency = Module.reoneWebFsMirrorConcurrency | 0;
        }

        async function fetchOne(relPath) {
            var normLower = relPath.toLowerCase();
            var url = httpGameFileUrl(relPath);
            var fr = await fetch(url, { credentials: "same-origin" });
            if (!fr.ok) {
                throw new Error("reone web: " + url + " -> HTTP " + fr.status);
            }
            var buf = await fr.arrayBuffer();
            return { normLower: normLower, buf: buf };
        }

        for (var start = 0; start < filtered.length; start += mirrorConcurrency) {
            var batch = filtered.slice(start, start + mirrorConcurrency);
            var results = await Promise.all(batch.map(function (rp) {
                return fetchOne(rp);
            }));
            for (var b = 0; b < results.length; ++b) {
                var item = results[b];
                var fsPath = "/game/" + item.normLower;
                var parentFs = fsPath.slice(0, fsPath.lastIndexOf("/"));
                ensureDirectory(parentFs);
                FS.writeFile(fsPath, new Uint8Array(item.buf));
                loaded++;
            }
            if ((loaded & 31) === 0 || loaded === filtered.length) {
                setGateLoadingMessage(
                    "Loaded " +
                        loaded +
                        " files from server mirror… (subset of " +
                        totalListed +
                        " listed; skipped heavy folders — see gamefs.js)"
                );
                await new Promise(function (r) {
                    setTimeout(r, 0);
                });
            }
        }
        setGateLoadingMessage("Finished (" + loaded + " files). Starting engine…");
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
                var parentFs = fsPath.slice(0, fsPath.lastIndexOf("/"));
                ensureDirectory(parentFs);

                var file = await entry.getFile();
                var buf = await file.arrayBuffer();
                FS.writeFile(fsPath, new Uint8Array(buf));
                fileCount++;
                if ((fileCount & 63) === 0) {
                    setGateLoadingMessage(
                        "Copied " +
                            fileCount +
                            " files into browser memory…" +
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
            "Copying your KotOR install into browser memory (first launch can take several minutes). " +
                "movies/, streammusic/, and saves/ are skipped by default — set Module.reoneWebFsNoSkip=true before engine.js to mount everything."
        );

        await walk(rootHandle, "");
        setGateLoadingMessage("Finished (" + fileCount + " files). Starting engine…");
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
                            "Pick your KotOR install folder below (same as KotOR.js), or fix serve.py --game-root + game-manifest.json."
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
