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

    /** MEMFS paths must keep manifest/install casing; C++ directory_iterator is case-sensitive. */
    function gameMemfsPath(relOriginal) {
        return "/game/" + String(relOriginal || "").replace(/\\/g, "/");
    }

    function ensureGameFileParentMemfs(relOriginal) {
        var rel = String(relOriginal || "").replace(/\\/g, "/");
        var parts = rel.split("/").filter(Boolean);
        if (parts.length > 1) {
            ensureDirectory("/game/" + parts.slice(0, -1).join("/"));
        } else {
            ensureDirectory("/game");
        }
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
            : 24;

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
            : 2 * 1024 * 1024;

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

    function parseInflightKey(key) {
        var pipe = key.lastIndexOf("|");
        var colon = key.lastIndexOf(":");
        if (pipe <= 0 || colon <= pipe + 1) {
            return null;
        }
        var url = key.slice(0, pipe);
        var start = Number(key.slice(pipe + 1, colon));
        var end = Number(key.slice(colon + 1));
        if (!Number.isFinite(start) || !Number.isFinite(end)) {
            return null;
        }
        return { url: url, start: start, end: end };
    }

    function mirrorRangeCacheGetContaining(url, rangeStart, rangeEndInclusive) {
        if (
            typeof rangeStart !== "number" ||
            !Number.isFinite(rangeStart) ||
            typeof rangeEndInclusive !== "number" ||
            !Number.isFinite(rangeEndInclusive)
        ) {
            return null;
        }
        for (var i = mirrorRangeCacheLru.length - 1; i >= 0; --i) {
            var key = mirrorRangeCacheLru[i];
            var parsed = parseInflightKey(key);
            if (!parsed || parsed.url !== url) {
                continue;
            }
            if (parsed.start > rangeStart || parsed.end < rangeEndInclusive) {
                continue;
            }
            var e = mirrorRangeCache[key];
            if (!e || !e.bytes) {
                continue;
            }
            var from = rangeStart - parsed.start;
            var to = from + (rangeEndInclusive - rangeStart + 1);
            if (from < 0 || to > e.bytes.length) {
                continue;
            }
            mirrorRangeCacheTouch(key);
            return { bytes: e.bytes.subarray(from, to), total: e.total };
        }
        return null;
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
        var containing = mirrorRangeCacheGetContaining(url, rangeStart, endInclusive);
        if (containing) {
            return Promise.resolve(containing);
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
    /** Prefix bytes for huge BIFs (header + variable resource table) — KeyBif::init reads this slice only. */
    var mirrorBifPrefixBytes = {};
    // Whole-file RAM cache only for *small* mirror files. Larger files (e.g. dialog.tlk ~5 MiB) previously used a
    // single Range for bytes=0-(N-1); combined with Asyncify + concurrent fetch slots that could stall headless runs.
    // Override with Module.reoneWebMirrorFullCacheMaxBytes (e.g. 64 << 20) when profiling desktop browsers only.
    var MIRROR_FULL_CACHE_MAX =
        typeof Module.reoneWebMirrorFullCacheMaxBytes === "number" && Module.reoneWebMirrorFullCacheMaxBytes > 0
            ? Module.reoneWebMirrorFullCacheMaxBytes
            : 24 * 1024 * 1024;
    /** Boot archives listed in HTTP_MIRROR_BOOT_FILES may exceed MIRROR_FULL_CACHE_MAX (gui ERF ~106 MiB). */
    var MIRROR_BOOT_FULL_CACHE_MAX =
        typeof Module.reoneWebMirrorBootFullCacheMaxBytes === "number" &&
        Module.reoneWebMirrorBootFullCacheMaxBytes > 0
            ? Module.reoneWebMirrorBootFullCacheMaxBytes
            : 160 * 1024 * 1024;
    // Default off: fire-and-forget prefetches compete for the same fetch slot pool as synchronous reads under Asyncify.
    // Set Module.reoneWebMirrorPrefetchBytes (e.g. 1<<20) to re-enable read-ahead tuning on desktop.
    var MIRROR_PREFETCH_BYTES =
        typeof Module.reoneWebMirrorPrefetchBytes === "number" && Module.reoneWebMirrorPrefetchBytes > 0
            ? Module.reoneWebMirrorPrefetchBytes
            : 0;

    function queueMirrorPrefetch(relLower, originalPath, offset) {
        if (!originalPath || typeof offset !== "number" || !Number.isFinite(offset) || offset < 0) {
            return;
        }
        var total = mirrorLengthCache[relLower];
        if (typeof total === "number" && total >= 0 && offset >= total) {
            return;
        }
        fetchMirrorRange(originalPath, offset, MIRROR_PREFETCH_BYTES).catch(function () {});
    }

    async function mirrorHttpHeadTotalBytes(relLower, originalPath) {
        var url = httpGameFileUrl(originalPath);
        var resp = await fetch(url, { method: "HEAD", credentials: "same-origin" });
        if (!resp.ok) {
            throw new Error("reone web: HEAD " + url + " -> HTTP " + resp.status);
        }
        var contentRange = resp.headers.get("Content-Range") || "";
        var slash = contentRange.lastIndexOf("/");
        if (slash !== -1) {
            var total = Number(contentRange.slice(slash + 1));
            if (Number.isFinite(total)) {
                mirrorLengthCache[relLower] = total;
                return total;
            }
        }
        var len = Number(resp.headers.get("Content-Length") || "-1");
        if (Number.isFinite(len) && len >= 0) {
            mirrorLengthCache[relLower] = len;
            return len;
        }
        return -1;
    }

    async function mirrorHttpTotalBytes(relLower, originalPath) {
        if (Object.prototype.hasOwnProperty.call(mirrorLengthCache, relLower)) {
            return mirrorLengthCache[relLower];
        }
        if (mirrorLengthInflight[relLower]) {
            return mirrorLengthInflight[relLower];
        }
        mirrorLengthInflight[relLower] = (async function () {
            try {
                try {
                    var headTotal = await mirrorHttpHeadTotalBytes(relLower, originalPath);
                    if (headTotal >= 0) {
                        return headTotal;
                    }
                } catch (headErr) {
                    console.warn("reone web: HEAD stat failed, falling back to Range:", originalPath, headErr);
                }
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

    var HTTP_MIRROR_BOOT_FILES = [
        "chitin.key",
        "dialog.tlk",
        "patch.erf",
        "lips/global.mod",
        "lips/localization.mod",
        "TexturePacks/swpc_tex_gui.erf",
        "TexturePacks/swpc_tex_tpc.erf",
    ];

    /** Read exactly len bytes from mirror/prefix cache (loops Range fetches when responses are short). */
    async function readMirrorBytesExact(rel, original, offset, len) {
        if (len <= 0) {
            return new Uint8Array(0);
        }
        var prefix = mirrorBifPrefixBytes[rel];
        if (prefix && prefix.length > 0 && offset < prefix.length) {
            var endInPrefix = Math.min(offset + len, prefix.length);
            var fromPrefix = prefix.subarray(offset, endInPrefix);
            if (fromPrefix.length >= len) {
                return new Uint8Array(fromPrefix);
            }
            var tailExact = await readMirrorBytesExact(rel, original, offset + fromPrefix.length, len - fromPrefix.length);
            if (!tailExact || tailExact.length === 0) {
                return fromPrefix.length ? new Uint8Array(fromPrefix) : null;
            }
            var mergedPrefix = new Uint8Array(fromPrefix.length + tailExact.length);
            mergedPrefix.set(fromPrefix, 0);
            mergedPrefix.set(tailExact, fromPrefix.length);
            return mergedPrefix;
        }
        var out = new Uint8Array(len);
        var got = 0;
        var emptyRetries = 0;
        while (got < len) {
            var chunk = await fetchMirrorRange(original, offset + got, len - got);
            if (!chunk || !chunk.bytes || chunk.bytes.length === 0) {
                if (++emptyRetries >= 24) {
                    break;
                }
                await new Promise(function (r) {
                    setTimeout(r, 50);
                });
                continue;
            }
            emptyRetries = 0;
            var n = Math.min(chunk.bytes.length, len - got);
            out.set(chunk.bytes.subarray(0, n), got);
            got += n;
        }
        if (got < len) {
            return null;
        }
        return out;
    }

    /** Fetch BIF variable resource tables for archives too large to MEMFS-cache whole (models.bif, sounds.bif). */
    async function warmHttpMirrorBifIndexTables(lookup) {
        for (var relKey in lookup) {
            if (!Object.prototype.hasOwnProperty.call(lookup, relKey)) {
                continue;
            }
            if (!/\.bif$/i.test(relKey)) {
                continue;
            }
            var original = lookup[relKey];
            var total = mirrorLengthCache[relKey];
            if (typeof total !== "number") {
                try {
                    total = await mirrorHttpTotalBytes(relKey, original);
                } catch (e) {
                    console.warn("reone web: BIF index stat failed for", original, e);
                    continue;
                }
            }
            if (typeof total !== "number" || total <= MIRROR_FULL_CACHE_MAX) {
                continue;
            }
            setGateLoadingMessage("Indexing BIF table " + original + "…");
            try {
                var hdr = await readMirrorBytesExact(relKey, original, 0, 20);
                if (!hdr || hdr.length < 20) {
                    continue;
                }
                var sig = String.fromCharCode.apply(null, hdr.subarray(0, 8));
                if (sig !== "BIFFV1  ") {
                    continue;
                }
                var dv = new DataView(hdr.buffer, hdr.byteOffset, hdr.byteLength);
                var nvar = dv.getUint32(8, true);
                var offTable = dv.getUint32(16, true);
                var tableEnd = offTable + nvar * 16;
                if (tableEnd > total || tableEnd < 20) {
                    continue;
                }
                var slice = await readMirrorBytesExact(relKey, original, 0, tableEnd);
                if (!slice || slice.length < tableEnd) {
                    console.warn(
                        "reone web: BIF index short read for",
                        original,
                        slice ? slice.length : 0,
                        "expected",
                        tableEnd
                    );
                    continue;
                }
                // Keep the index table in a plain JS cache. C++ pulls it synchronously via EM_ASM
                // (reoneWebGetBifIndexBytes) during KeyBif::init — no Asyncify, no preRun _malloc.
                // Do NOT _malloc/HEAPU8 here: warmHttpMirrorBifIndexTables runs in preRun, BEFORE
                // __wasm_call_ctors initializes the C++ allocator, so calling _malloc traps as
                // "null function or function signature mismatch".
                mirrorBifPrefixBytes[relKey] = new Uint8Array(slice);
            } catch (e) {
                console.warn("reone web: BIF index preload failed for", original, e);
            }
        }
    }

    /** Synchronous index-table accessor for C++ KeyBif::init. Returns Uint8Array or null. */
    Module.reoneWebGetBifIndexBytes = function (path) {
        var rel = normalizeGamePath(path);
        var bytes = mirrorBifPrefixBytes[rel];
        return bytes && bytes.length > 0 ? bytes : null;
    };

    Module.reoneWebGameFileMemfsComplete = function (path) {
        var rel = normalizeGamePath(path);
        var lookup = Module.reoneWebHttpMirrorFiles || {};
        var original = lookup[rel];
        if (!original) {
            return false;
        }
        var total = mirrorLengthCache[rel];
        if (typeof total !== "number" || total < 0) {
            return false;
        }
        try {
            var st = FS.stat(gameMemfsPath(original));
            return st.size === total;
        } catch (e) {
            return false;
        }
    };

    async function warmHttpMirrorBootFiles(lookup) {
        var rels = HTTP_MIRROR_BOOT_FILES.slice();
        for (var relKey in lookup) {
            if (!Object.prototype.hasOwnProperty.call(lookup, relKey)) {
                continue;
            }
            if (/^data\/.*\.bif$/i.test(relKey)) {
                rels.push(relKey);
            }
            if (/^lips\/(global|localization)\.mod$/i.test(relKey)) {
                rels.push(relKey);
            }
        }
        var seen = {};
        var bootSet = {};
        for (var bi = 0; bi < HTTP_MIRROR_BOOT_FILES.length; ++bi) {
            bootSet[String(HTTP_MIRROR_BOOT_FILES[bi]).toLowerCase()] = true;
        }
        for (var i = 0; i < rels.length; ++i) {
            var rel = String(rels[i]).toLowerCase();
            if (seen[rel]) {
                continue;
            }
            seen[rel] = true;
            var original = lookup[rel];
            if (!original) {
                continue;
            }
            setGateLoadingMessage("Preloading " + original + "…");
            try {
                var total = await mirrorHttpTotalBytes(rel, original);
                var maxFull = bootSet[rel] ? MIRROR_BOOT_FULL_CACHE_MAX : MIRROR_FULL_CACHE_MAX;
                if (typeof total === "number" && total >= 0 && total <= maxFull) {
                    await mirrorHttpEnsureFullBytes(rel, original, maxFull);
                }
            } catch (e) {
                console.warn("reone web: boot preload failed for", original, e);
            }
        }
    }

    function publishMirrorFullCacheToMemfs(relLower) {
        var full = mirrorFullBytes[relLower];
        if (!full || full.length === 0) {
            return;
        }
        var lookup = Module.reoneWebHttpMirrorFiles || {};
        var original = lookup[relLower] || relLower;
        ensureGameFileParentMemfs(original);
        var fsPath = gameMemfsPath(original);
        try {
            FS.writeFile(fsPath, full, { canOwn: true });
        } catch (e) {
            console.warn("reone web: MEMFS publish failed for", fsPath, e);
        }
    }

    async function mirrorHttpEnsureFullBytes(relLower, originalPath, maxBytesOverride) {
        if (mirrorFullBytes[relLower]) {
            publishMirrorFullCacheToMemfs(relLower);
            return mirrorFullBytes[relLower];
        }
        if (mirrorFullInflight[relLower]) {
            return mirrorFullInflight[relLower];
        }
        var total = mirrorLengthCache[relLower];
        var maxFull =
            typeof maxBytesOverride === "number" && maxBytesOverride > 0
                ? maxBytesOverride
                : MIRROR_FULL_CACHE_MAX;
        if (typeof total !== "number" || total < 0 || total > maxFull) {
            return null;
        }
        mirrorFullInflight[relLower] = (async function () {
            try {
                var r =
                    total === 0
                        ? { bytes: new Uint8Array(0), total: 0 }
                        : await fetchMirrorRange(originalPath, 0, total);
                var bytes = r.bytes || new Uint8Array(0);
                if (total > 0 && bytes.length !== total) {
                    throw new Error(
                        "reone web: full preload size mismatch for " +
                            originalPath +
                            " (expected " +
                            total +
                            ", got " +
                            bytes.length +
                            ")"
                    );
                }
                mirrorFullBytes[relLower] = bytes;
                publishMirrorFullCacheToMemfs(relLower);
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
        queueGateReadStatus("Loading " + original + " (stat)…");
        try {
            return await mirrorHttpTotalBytes(rel, original);
        } catch (e) {
            console.error("reone web: stat failed:", rel, e);
            return -1;
        }
    };

    var reoneReadSeq = 0;
    function reoneReadWatchdog(seq, rel, offset, len) {
        return setTimeout(function () {
            var inflightKeys = Object.keys(mirrorFetchInflight);
            console.warn(
                "reone web: READ STALL seq=" + seq + " " + rel + " off=" + offset + " len=" + len +
                    " | mirrorFetchActive=" + mirrorFetchActive +
                    " waiters=" + mirrorFetchWaiters.length +
                    " inflight=" + inflightKeys.length +
                    " :: " + inflightKeys.slice(0, 8).join(" , ")
            );
        }, 3000);
    }

    Module.reoneWebFileReadAsync = async function (path, offset, len) {
        var rel = normalizeGamePath(path);
        if (len <= 0) {
            return new Uint8Array(0);
        }
        var __seq = ++reoneReadSeq;
        var __wd = reoneReadWatchdog(__seq, rel, offset, len);
        try {
            return await reoneWebFileReadAsyncImpl(rel, offset, len);
        } finally {
            clearTimeout(__wd);
        }
    };

    async function reoneWebFileReadAsyncImpl(rel, offset, len) {
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
        queueGateReadStatus(
            "Loading " +
                original +
                " (" +
                Math.max(0, Math.floor(offset)) +
                "+" +
                Math.max(0, Math.floor(len)) +
                ")…"
        );
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
                    var slice = full.subarray(offset, end);
                    if (slice.length >= len) {
                        return new Uint8Array(slice);
                    }
                    var tailFull = await readMirrorBytesExact(rel, original, offset + slice.length, len - slice.length);
                    if (!tailFull || tailFull.length === 0) {
                        return slice.length ? new Uint8Array(slice) : null;
                    }
                    var outFull = new Uint8Array(slice.length + tailFull.length);
                    outFull.set(slice, 0);
                    outFull.set(tailFull, slice.length);
                    return outFull;
                }
            }
            var exact = await readMirrorBytesExact(rel, original, offset, len);
            if (exact && exact.length > 0) {
                queueMirrorPrefetch(rel, original, offset + exact.length);
            }
            return exact;
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
            ensureDirectory(gameMemfsPath(relDir));
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
            ensureEmptyFile(gameMemfsPath(relF));
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
        setGateLoadingMessage("Indexed " + indexed + " lazy game files. Preloading boot archives…");
        await warmHttpMirrorBootFiles(lookup);
        await warmHttpMirrorBifIndexTables(lookup);
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

    var gateReadStatus = {
        queued: false,
        latest: "",
    };

    function queueGateReadStatus(line) {
        gateReadStatus.latest = line;
        if (gateReadStatus.queued) {
            return;
        }
        gateReadStatus.queued = true;
        var flush = function () {
            gateReadStatus.queued = false;
            if (gateReadStatus.latest) {
                setGateLoadingMessage(gateReadStatus.latest);
            }
        };
        if (typeof window !== "undefined" && typeof window.requestAnimationFrame === "function") {
            window.requestAnimationFrame(flush);
            return;
        }
        setTimeout(flush, 100);
    }

    /** True when no mirror fetch is in flight (safe to start module load without contending with boot I/O). */
    Module.reoneWebLazyIoIdle = function () {
        if (mirrorFetchActive > 0) {
            return false;
        }
        if (mirrorFetchWaiters.length > 0) {
            return false;
        }
        if (Object.keys(mirrorFetchInflight).length > 0) {
            return false;
        }
        if (Object.keys(mirrorFullInflight).length > 0) {
            return false;
        }
        return true;
    };

    Module.reoneWebOnEngineReady = function () {
        setGateLoadingMessage("Main menu ready.");
        removeGateUi();
        Module.reoneWebMenuReady = true;
    };

    // Fired from C++ (Game::loadModule) once the area is loaded and the in-game screen is up.
    Module.reoneWebOnModuleReady = function () {
        Module.reoneWebModuleReady = true;
        console.log("reone web: module ready (in-game).");
    };

    /** Must match tools/web/module_mirror.json and Installation::moduleArchiveRelPaths. */
    var DEFAULT_MODULE_ARCHIVE_TEMPLATES = [
        "modules/{module}.rim",
        "modules/{module}_s.rim",
        "modules/{module}.mod",
        "modules/{module}.erf",
        "modules/{module}_loc.mod",
        "modules/{module}_loc.erf",
        "modules/{module}_dlg.mod",
        "modules/{module}_dlg.erf",
        "lips/{module}_loc.mod",
        "lips/{module}_loc.erf",
    ];

    var _moduleArchiveTemplates = null;
    var _moduleArchiveTemplatesPromise = null;

    function expandModuleMirrorPaths(moduleName, templates) {
        var low = String(moduleName || "").toLowerCase();
        if (!low) {
            return [];
        }
        return templates.map(function (t) {
            return String(t).replace(/\{module\}/g, low);
        });
    }

    async function loadModuleArchiveTemplates() {
        if (_moduleArchiveTemplates) {
            return _moduleArchiveTemplates;
        }
        if (!_moduleArchiveTemplatesPromise) {
            _moduleArchiveTemplatesPromise = (async function () {
                try {
                    var resp = await fetch("/module_mirror.json", { credentials: "same-origin" });
                    if (resp.ok) {
                        var data = await resp.json();
                        if (data && Array.isArray(data.moduleArchiveRelPaths) && data.moduleArchiveRelPaths.length) {
                            _moduleArchiveTemplates = data.moduleArchiveRelPaths;
                            return _moduleArchiveTemplates;
                        }
                    }
                } catch (e) {
                    /* same-origin fetch unavailable — use defaults */
                }
                _moduleArchiveTemplates = DEFAULT_MODULE_ARCHIVE_TEMPLATES.slice();
                return _moduleArchiveTemplates;
            })();
        }
        return _moduleArchiveTemplatesPromise;
    }

    /** Primary module containers mirrored by ResourceDirector::loadModuleResources. */
    function moduleEssentialMirrorRelPaths(moduleName) {
        var low = String(moduleName || "").toLowerCase();
        if (!low) {
            return [];
        }
        return ["modules/" + low + ".rim", "modules/" + low + ".mod"];
    }

    /** Relative mirror paths needed to load a KotOR module (rim / _s / lips / dlg). */
    async function moduleMirrorRelPaths(moduleName) {
        var templates = await loadModuleArchiveTemplates();
        return expandModuleMirrorPaths(moduleName, templates);
    }

    /**
     * Fetch module archives into MEMFS so C++ can use FileInputStream (avoids racing Asyncify
     * range reads against boot-time BIF traffic during warp / New Game).
     */
    Module.reoneWebPreloadModuleFiles = async function (moduleName) {
        var lookup = Module.reoneWebHttpMirrorFiles || {};
        var rels = await moduleMirrorRelPaths(moduleName);
        var loaded = 0;
        for (var i = 0; i < rels.length; ++i) {
            var rel = rels[i];
            var original = lookup[rel];
            if (!original) {
                continue;
            }
            setGateLoadingMessage("Preloading module file " + original + "…");
            try {
                var total = mirrorLengthCache[rel];
                if (typeof total !== "number") {
                    total = await mirrorHttpTotalBytes(rel, original);
                }
                if (typeof total !== "number" || total < 0) {
                    console.warn("reone web: module preload stat failed for", original);
                    continue;
                }
                if (total <= MIRROR_FULL_CACHE_MAX) {
                    await mirrorHttpEnsureFullBytes(rel, original);
                } else {
                    console.warn(
                        "reone web: module file too large to MEMFS-cache whole file:",
                        original,
                        total
                    );
                    continue;
                }
                if (Module.reoneWebGameFileMemfsComplete("/game/" + rel)) {
                    loaded++;
                }
            } catch (e) {
                console.warn("reone web: module preload failed for", original, e);
            }
        }
        console.log(
            "reone web: module preload finished for",
            moduleName,
            "(" + loaded + " files in MEMFS)"
        );
        return loaded;
    };

    /** Preload module archives, then queue a pending module load via reone_web_warp. */
    Module.reoneWebWarpAsync = async function (name) {
        var low = String(name || "").toLowerCase();
        if (!low) {
            console.error("reone web: warp module name required");
            return false;
        }
        await Module.reoneWebPreloadModuleFiles(name);
        var lookup = Module.reoneWebHttpMirrorFiles || {};
        var essential = moduleEssentialMirrorRelPaths(name);
        var manifestHasPrimary = false;
        var primaryReady = false;
        for (var ei = 0; ei < essential.length; ++ei) {
            var erel = essential[ei];
            if (!lookup[erel]) {
                continue;
            }
            manifestHasPrimary = true;
            if (Module.reoneWebGameFileMemfsComplete("/game/" + erel)) {
                primaryReady = true;
                break;
            }
        }
        if (manifestHasPrimary && !primaryReady) {
            console.error(
                "reone web: primary module archive not in MEMFS after preload; refusing warp for",
                name
            );
            return false;
        }
        return Module.reoneWebWarp(name);
    };

    // Load a KotOR module by name (e.g. "end_m01aa") from JS. This is what the smoke / a future
    // "New Game" shim calls after the menu is ready; it reuses the supported `warp` console command
    // via the EMSCRIPTEN_KEEPALIVE export reone_web_warp(const char*). Module resrefs are ASCII.
    Module.reoneWebWarp = function (name) {
        var warp = Module._reone_web_warp;
        if (typeof warp !== "function") {
            console.error("reone web: _reone_web_warp export not available yet.");
            return false;
        }
        var s = String(name || "");
        var bytes = [];
        for (var i = 0; i < s.length; ++i) {
            bytes.push(s.charCodeAt(i) & 0x7f);
        }
        bytes.push(0);
        var ptr = _malloc(bytes.length);
        HEAPU8.set(bytes, ptr);
        try {
            warp(ptr);
        } finally {
            _free(ptr);
        }
        return true;
    };

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
        setGateLoadingMessage("Preloading boot archives from your install…");
        for (var bi = 0; bi < HTTP_MIRROR_BOOT_FILES.length; ++bi) {
            var bootRel = HTTP_MIRROR_BOOT_FILES[bi].toLowerCase();
            var bootHandle = handleFiles[bootRel];
            if (!bootHandle) {
                continue;
            }
            try {
                await bootHandle.getFile();
            } catch (e) {
                console.warn("reone web: boot handle preload failed:", bootRel, e);
            }
        }
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
                        setGateLoadingMessage("Indexed lazy files. Fetching game resources…");
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
                setGateLoadingMessage("Indexed lazy files. Fetching game resources…");
                done();
            })
            .catch(function (err) {
                console.error("reone web: game filesystem setup failed:", err);
                done();
            });
    });
})();
