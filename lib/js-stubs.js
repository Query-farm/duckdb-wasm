addToLibrary({
    duckdb_web_test_platform_feature__sig: 'ii',
    duckdb_web_test_platform_feature: function (feature) {
        return globalThis.DUCKDB_RUNTIME.testPlatformFeature(Module, feature);
    },
    duckdb_web_fs_get_default_data_protocol__sig: 'i',
    duckdb_web_fs_get_default_data_protocol: function (Module) {
        return globalThis.DUCKDB_RUNTIME.getDefaultDataProtocol(Module);
    },
    duckdb_web_fs_file_open__sig: 'pii',
    duckdb_web_fs_file_open: function (fileId, flags) {
        return globalThis.DUCKDB_RUNTIME.openFile(Module, fileId, flags);
    },
    duckdb_web_fs_file_sync__sig: 'vi',
    duckdb_web_fs_file_sync: function (fileId) {
        return globalThis.DUCKDB_RUNTIME.syncFile(Module, fileId);
    },
    duckdb_web_fs_file_drop_file__sig: 'vpi',
    duckdb_web_fs_file_drop_file: function (fileName, fileNameLen) {
        return globalThis.DUCKDB_RUNTIME.dropFile(Module, fileName, fileNameLen);
    },
    duckdb_web_fs_file_close__sig: 'vi',
    duckdb_web_fs_file_close: function (fileId) {
        return globalThis.DUCKDB_RUNTIME.closeFile(Module, fileId);
    },
    duckdb_web_fs_file_truncate__sig: 'vid',
    duckdb_web_fs_file_truncate: function (fileId, newSize) {
        return globalThis.DUCKDB_RUNTIME.truncateFile(Module, fileId, newSize);
    },
    duckdb_web_fs_file_read__sig: 'iipid',
    duckdb_web_fs_file_read: function (fileId, buf, size, location) {
        return globalThis.DUCKDB_RUNTIME.readFile(Module, fileId, buf, size, location);
    },
    duckdb_web_fs_file_write__sig: 'iipid',
    duckdb_web_fs_file_write: function (fileId, buf, size, location) {
        return globalThis.DUCKDB_RUNTIME.writeFile(Module, fileId, buf, size, location);
    },
    duckdb_web_fs_file_get_last_modified_time__sig: 'di',
    duckdb_web_fs_file_get_last_modified_time: function (fileId) {
        return globalThis.DUCKDB_RUNTIME.getLastFileModificationTime(Module, fileId);
    },
    duckdb_web_fs_directory_exists__sig: 'ipi',
    duckdb_web_fs_directory_exists: function (path, pathLen) {
        return globalThis.DUCKDB_RUNTIME.checkDirectory(Module, path, pathLen);
    },
    duckdb_web_fs_directory_create__sig: 'vpi',
    duckdb_web_fs_directory_create: function (path, pathLen) {
        return globalThis.DUCKDB_RUNTIME.createDirectory(Module, path, pathLen);
    },
    duckdb_web_fs_directory_remove__sig: 'vpi',
    duckdb_web_fs_directory_remove: function (path, pathLen) {
        return globalThis.DUCKDB_RUNTIME.removeDirectory(Module, path, pathLen);
    },
    duckdb_web_fs_directory_list_files__sig: 'ipi',
    duckdb_web_fs_directory_list_files: function (path, pathLen) {
        return globalThis.DUCKDB_RUNTIME.listDirectoryEntries(Module, path, pathLen);
    },
    duckdb_web_fs_glob__sig: 'vpi',
    duckdb_web_fs_glob: function (path, pathLen) {
        return globalThis.DUCKDB_RUNTIME.glob(Module, path, pathLen);
    },
    duckdb_web_fs_file_move__sig: 'vpipi',
    duckdb_web_fs_file_move: function (from, fromLen, to, toLen) {
        return globalThis.DUCKDB_RUNTIME.moveFile(Module, from, fromLen, to, toLen);
    },
    duckdb_web_fs_file_exists__sig: 'ipi',
    duckdb_web_fs_file_exists: function (path, pathLen) {
        return globalThis.DUCKDB_RUNTIME.checkFile(Module, path, pathLen);
    },
    duckdb_web_fs_file_remove: function (path, pathLen) {
        return globalThis.DUCKDB_RUNTIME.removeFile(Module, path, pathLen);
    },
    duckdb_web_udf_scalar_call__sig: 'vpipipi',
    duckdb_web_udf_scalar_call: function (funcId, descPtr, descSize, ptrsPtr, ptrsSize, response) {
        return globalThis.DUCKDB_RUNTIME.callScalarUDF(Module, funcId, descPtr, descSize, ptrsPtr, ptrsSize, response);
    },
    sched_getcpu__sig: 'i',
    sched_getcpu: function () {
        return 0;
    },
    // --- Web Crypto stubs (for WASM extensions that need crypto without OpenSSL) ---
    // Fill buffer with cryptographically secure random bytes via Web Crypto API.
    duckdb_wasm_crypto_random__sig: 'vpi',
    duckdb_wasm_crypto_random: function (buf, len) {
        crypto.getRandomValues(Module.HEAPU8.subarray(buf, buf + len));
    },
    // duckdb_wasm_sha256 is implemented in C++ (http_wasm.cc) using DuckDB's MbedTLS,
    // not as a JS stub. It's a real WASM function available to side modules.
    // --- OAuth popup stub (for WASM extensions doing PKCE) ---
    // Open a URL on the main thread (popup), block until auth code comes back via SAB.
    // Returns malloc'd string (auth code) or 0 on failure. Caller frees.
    // SAB layout: [flag:Int32][dataLen:Int32][data:UTF-8]  flag: 0=waiting, 1=ready, -1=error
    duckdb_wasm_open_auth_url__sig: 'pii',
    duckdb_wasm_open_auth_url: function (url_ptr, timeout_ms) {
        var url = UTF8ToString(url_ptr);
        if (typeof oauthInt32 === 'undefined' || !oauthInt32) return 0;
        Atomics.store(oauthInt32, 0, 0);
        postMessage({ type: 'open-auth-url', url: url });
        // Block until auth code arrives or timeout expires.
        // Use 2-second polling intervals so main thread can signal popup-closed.
        var elapsed = 0;
        var interval = 2000;
        while (Atomics.load(oauthInt32, 0) === 0) {
            var result = Atomics.wait(oauthInt32, 0, 0, interval);
            if (result === 'timed-out') {
                elapsed += interval;
                if (timeout_ms > 0 && elapsed >= timeout_ms) {
                    globalThis._duckdb_wasm_auth_error = 'Authentication timed out';
                    return 0;
                }
                // Ask main thread if popup is still open
                // (main thread can set flag to -1 if it detects closure)
            }
        }
        var flag = Atomics.load(oauthInt32, 0);
        var dv = new DataView(oauthSAB);
        var len = dv.getInt32(4, true);
        if (len <= 0) return 0;
        if (flag === -1) {
            globalThis._duckdb_wasm_auth_error = '';
            for (var i = 0; i < len; i++) globalThis._duckdb_wasm_auth_error += String.fromCharCode(oauthBytes[8 + i]);
            return 0;
        }
        var buf = _malloc(len + 1);
        if (buf === 0) return 0;
        for (var i = 0; i < len; i++) Module.HEAPU8[buf + i] = oauthBytes[8 + i];
        Module.HEAPU8[buf + len] = 0;
        return buf;
    },
    duckdb_wasm_get_auth_error__sig: 'pi',
    duckdb_wasm_get_auth_error: function (unused) {
        var err = globalThis._duckdb_wasm_auth_error || '';
        if (!err) return 0;
        var len = lengthBytesUTF8(err) + 1;
        var buf = _malloc(len);
        if (buf === 0) return 0;
        stringToUTF8(err, buf, len);
        return buf;
    },
    // Return the page origin (e.g. "http://localhost:8765") so extensions can
    // construct OAuth redirect URIs that match the current deployment.
    // Reads globalThis._duckdb_page_origin first (explicit override for blob: URL workers),
    // falls back to self.location.origin.
    duckdb_wasm_get_page_origin__sig: 'p',
    duckdb_wasm_get_page_origin: function () {
        var origin = globalThis._duckdb_page_origin ||
                     (typeof self !== 'undefined' && self.location ? self.location.origin : '');
        if (!origin || origin === 'null' || origin.startsWith('blob:')) return 0;
        var len = lengthBytesUTF8(origin) + 1;
        var buf = _malloc(len);
        if (buf === 0) return 0;
        stringToUTF8(origin, buf, len);
        return buf;
    },
    _emscripten_yield__sig: 'vd',
    _emscripten_yield: function (now) {
        // Override emscripten's busy-spin yield with Atomics.wait to actually block the thread.
        // Called by emscripten_thread_sleep() in a loop that checks elapsed time.
        // This makes nanosleep/usleep/std::this_thread::sleep_for work in Web Workers
        // without -pthread, by blocking for 100ms per yield call instead of spinning.
        if (typeof SharedArrayBuffer !== 'undefined') {
            try {
                Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 100);
            } catch (e) {
                // Atomics.wait throws on the main thread — fall through to no-op
            }
        }
    },
});
