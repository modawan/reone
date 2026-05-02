// KobaltBlu/KotOR.js-style browser game dir: full install under /game via the File System Access API (chitin.key at root).
// Bundled via Emscripten --pre-js with the WASM module (same class of setup as KotOR.js glue, not a separate shell script).
//
// Emscripten FS.createLazyFile uses synchronous XHR / worker-only paths and aborts on the browser main thread, so we copy
// each picked file into MEMFS with FS.writeFile after file.arrayBuffer(). Optional skips reduce RAM/time (movies/music/saves).
// Set Module.reoneWebFsNoSkip = true before engine.js for a full tree like KotOR.js exposes (needs enough browser RAM).
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

                if (typeof window.showDirectoryPicker !== "function") {
                    injectGateUi();
                    showGateError(
                        "This browser does not support the File System Access API (same limitation as KotOR.js in non-Chromium browsers). Use Chrome or Edge, or build with CMake embedded preload (REONE_WEB_ASSET_PROFILE full/custom); see doc/webassembly.md."
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
