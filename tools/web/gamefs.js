// browser game dir: full install under /game via the File System Access API (chitin.key at root).
// Bundled via Emscripten --pre-js with the WASM module.
//
// Keep game data out of the wasm bundle: index the selected folder into lightweight /game marker files,
// then let WebFileInputStream read exact byte ranges from FileSystemFileHandle/Blob.slice on demand.
// Optional skips reduce startup indexing work (movies/music/saves); set Module.reoneWebFsNoSkip = true to index all folders.
//
// Dev/automation: if same-origin GET /game-manifest.json succeeds (serve.py --game-root), we index listed files and
// read /game-files/... with Range requests on demand — no game data in git or embedded wasm artifacts.
//
// Retail / folder-picker testing: open engine.html?reoneFs=picker — skips the HTTP mirror probe entirely (no
// /game-manifest.json fetch). Same as Module.reoneWebFsDisableHttpMirror = true before engine.js loads.
(function () {
    if (typeof Module === "undefined") {
        Module = {};
    }

    try {
        if (typeof location !== "undefined" && location.search) {
            var qMode = new URLSearchParams(location.search).get("reoneFs");
            if (qMode) {
                var m = String(qMode).toLowerCase();
                if (m === "picker" || m === "fs" || m === "access") {
                    Module.reoneWebFsDisableHttpMirror = true;
                }
            }
        }
    } catch (e) {
        /* ignore malformed URL in non-browser shells */
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

    /** Limit parallel mirror fetches — Chromium hits ERR_INVALID_ARGUMENT when hundreds of Range requests overlap. */
    var mirrorFetchInflight = {};
    var mirrorFetchActive = 0;
    var mirrorFetchWaiters = [];
    var mirrorFetchMaxConcurrent =
        typeof Module.reoneWebMirrorMaxConcurrentFetches === "number" &&
        Module.reoneWebMirrorMaxConcurrentFetches > 0
            ? Module.reoneWebMirrorMaxConcurrentFetches
            : 8;

    /**
     * Completed Range responses (inflight only dedupes *concurrent* identical keys). Without this,
     * sequential repeats — e.g. many streams hitting the same BIF slice — hammer the server (lightmaps4.bif storms).
     */
    var mirrorRangeCache = {};
    var mirrorRangeCacheLru = [];
    var MIRROR_RANGE_CACHE_MAX_KEYS =
        typeof Module.reoneWebMirrorRangeCacheMaxKeys === "number" && Module.reoneWebMirrorRangeCacheMaxKeys > 0
            ? Module.reoneWebMirrorRangeCacheMaxKeys
            : 2048;
    var MIRROR_RANGE_CACHE_MAX_BYTES =
        typeof Module.reoneWebMirrorRangeCacheMaxBytesPerRange === "number" &&
        Module.reoneWebMirrorRangeCacheMaxBytesPerRange > 0
            ? Module.reoneWebMirrorRangeCacheMaxBytesPerRange
            : 512 * 1024;

    function mirrorRangeCacheTouch(key) {
        var i = mirrorRangeCacheLru.indexOf(key);
        if (i >= 0) {
            mirrorRangeCacheLru.splice(i, 1);
        }
        mirrorRangeCacheLru.push(key);
    }

    function mirrorRangeCacheRemember(key, bytes, total) {
        if (!bytes || bytes.byteLength === 0 || bytes.byteLength > MIRROR_RANGE_CACHE_MAX_BYTES) {
            return;
        }
        while (mirrorRangeCacheLru.length >= MIRROR_RANGE_CACHE_MAX_KEYS) {
            var drop = mirrorRangeCacheLru.shift();
            if (drop) {
                delete mirrorRangeCache[drop];
            }
        }
        mirrorRangeCache[key] = {
            bytes: new Uint8Array(bytes),
            total: typeof total === "number" ? total : -1,
        };
        mirrorRangeCacheTouch(key);
    }

    function mirrorRangeCacheGet(key) {
        var e = mirrorRangeCache[key];
        if (!e) {
            return null;
        }
        mirrorRangeCacheTouch(key);
        return { bytes: e.bytes, total: e.total };
    }

    function mirrorFetchReleaseSlot() {
        mirrorFetchActive--;
        var w = mirrorFetchWaiters.shift();
        if (w) {
            w();
        }
    }

    function mirrorFetchAcquireSlot() {
        return new Promise(function (resolve) {
            function tryEnter() {
                if (mirrorFetchActive < mirrorFetchMaxConcurrent) {
                    mirrorFetchActive++;
                    resolve(mirrorFetchReleaseSlot);
                } else {
                    mirrorFetchWaiters.push(tryEnter);
                }
            }
            tryEnter();
        });
    }

    async function fetchMirrorRange(relPosix, rangeStart, rangeLen) {
        var url = httpGameFileUrl(relPosix);
        var endInclusive =
            typeof rangeStart === "number"
                ? typeof rangeLen === "number" && rangeLen > 0
                    ? rangeStart + rangeLen - 1
                    : rangeStart
                : "full";
        var inflightKey = url + "|" + rangeStart + ":" + endInclusive;
        var cached = mirrorRangeCacheGet(inflightKey);
        if (cached) {
            return Promise.resolve(cached);
        }
        if (mirrorFetchInflight[inflightKey]) {
            return mirrorFetchInflight[inflightKey];
        }
        mirrorFetchInflight[inflightKey] = (async function () {
            var releaseSlot = await mirrorFetchAcquireSlot();
            try {
                var headers = {};
                if (typeof rangeStart === "number") {
                    headers.Range = "bytes=" + rangeStart + "-" + endInclusive;
                }
                var resp = await fetch(url, { credentials: "same-origin", headers: headers });
                if (resp.status === 416) {
                    return { bytes: new Uint8Array(0), total: -1 };
                }
                if (!resp.ok && resp.status !== 206) {
                    throw new Error("reone web: " + url + " -> HTTP " + resp.status);
                }
                var buf = await resp.arrayBuffer();
                var out = {
                    bytes: new Uint8Array(buf),
                    total: parseContentLengthFromHeaders(resp.headers, buf.byteLength),
                };
                mirrorRangeCacheRemember(inflightKey, out.bytes, out.total);
                return out;
            } finally {
                delete mirrorFetchInflight[inflightKey];
                releaseSlot();
            }
        })();
        return mirrorFetchInflight[inflightKey];
    }

    /** HTTP mirror: many C++ streams stat the same path in parallel — dedupe length probes + cache small files in RAM. */
    var mirrorLengthCache = {};
    var mirrorLengthInflight = {};
    var mirrorFullBytes = {};
    var mirrorFullInflight = {};
    var MIRROR_FULL_CACHE_MAX = 8 * 1024 * 1024;

    async function mirrorHttpTotalBytes(relLower, originalPath) {
        if (Object.prototype.hasOwnProperty.call(mirrorLengthCache, relLower)) {
            return mirrorLengthCache[relLower];
        }
        if (mirrorLengthInflight[relLower]) {
            return mirrorLengthInflight[relLower];
        }
        mirrorLengthInflight[relLower] = (async function () {
            try {
                var r = await fetchMirrorRange(originalPath, 0, 1);
                var t = r.total;
                mirrorLengthCache[relLower] = t;
                return t;
            } finally {
                delete mirrorLengthInflight[relLower];
            }
        })();
        return mirrorLengthInflight[relLower];
    }

    async function mirrorHttpEnsureFullBytes(relLower, originalPath) {
        if (mirrorFullBytes[relLower]) {
            return mirrorFullBytes[relLower];
        }
        if (mirrorFullInflight[relLower]) {
            return mirrorFullInflight[relLower];
        }
        var total = mirrorLengthCache[relLower];
        if (typeof total !== "number" || total < 0 || total > MIRROR_FULL_CACHE_MAX) {
            return null;
        }
        mirrorFullInflight[relLower] = (async function () {
            try {
                var r =
                    total === 0
                        ? { bytes: new Uint8Array(0), total: 0 }
                        : await fetchMirrorRange(originalPath, 0, total);
                mirrorFullBytes[relLower] = r.bytes || new Uint8Array(0);
                return mirrorFullBytes[relLower];
            } finally {
                delete mirrorFullInflight[relLower];
            }
        })();
        return mirrorFullInflight[relLower];
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
            return await mirrorHttpTotalBytes(rel, original);
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
            var totalKnown = mirrorLengthCache[rel];
            if (
                typeof totalKnown === "number" &&
                totalKnown >= 0 &&
                totalKnown <= MIRROR_FULL_CACHE_MAX
            ) {
                var full = mirrorFullBytes[rel];
                if (!full) {
                    full = await mirrorHttpEnsureFullBytes(rel, original);
                }
                if (full && offset < full.length) {
                    var end = Math.min(offset + len, full.length);
                    return full.subarray(offset, end);
                }
            }
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
            if (man.files.length === 0) {
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

        if (indexed === 0) {
            return false;
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
