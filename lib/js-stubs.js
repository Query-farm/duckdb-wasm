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
    // SHA-256 hash: writes 32 bytes to out_hash. Uses Web Crypto (sync via Atomics.wait).
    // crypto.subtle.digest is async, so we use a small sync SHA-256 or fall back to
    // DuckDB's MbedTLS. For simplicity, implement SHA-256 in pure JS here.
    duckdb_wasm_sha256__sig: 'vpip',
    duckdb_wasm_sha256: function (data, len, out_hash) {
        var msg = Array.from(Module.HEAPU8.subarray(data, data + len));
        var K = [0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2];
        var H = [0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
        var bl = msg.length * 8;
        msg.push(0x80);
        while (msg.length % 64 !== 56) msg.push(0);
        for (var i = 7; i >= 0; i--) msg.push((bl >>> (i * 8)) & 0xff);
        for (var off = 0; off < msg.length; off += 64) {
            var w = new Array(64);
            for (var i = 0; i < 16; i++) w[i] = (msg[off+i*4]<<24)|(msg[off+i*4+1]<<16)|(msg[off+i*4+2]<<8)|msg[off+i*4+3];
            for (var i = 16; i < 64; i++) { var s0=((w[i-15]>>>7)|(w[i-15]<<25))^((w[i-15]>>>18)|(w[i-15]<<14))^(w[i-15]>>>3); var s1=((w[i-2]>>>17)|(w[i-2]<<15))^((w[i-2]>>>19)|(w[i-2]<<13))^(w[i-2]>>>10); w[i]=(w[i-16]+s0+w[i-7]+s1)|0; }
            var a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
            for (var i = 0; i < 64; i++) { var S1=((e>>>6)|(e<<26))^((e>>>11)|(e<<21))^((e>>>25)|(e<<7)); var ch=(e&f)^((~e)&g); var t1=(h+S1+ch+K[i]+w[i])|0; var S0=((a>>>2)|(a<<30))^((a>>>13)|(a<<19))^((a>>>22)|(a<<10)); var maj=(a&b)^(a&c)^(b&c); var t2=(S0+maj)|0; h=g;g=f;f=e;e=(d+t1)|0;d=c;c=b;b=a;a=(t1+t2)|0; }
            H[0]=(H[0]+a)|0;H[1]=(H[1]+b)|0;H[2]=(H[2]+c)|0;H[3]=(H[3]+d)|0;H[4]=(H[4]+e)|0;H[5]=(H[5]+f)|0;H[6]=(H[6]+g)|0;H[7]=(H[7]+h)|0;
        }
        for (var i = 0; i < 8; i++) { Module.HEAPU8[out_hash+i*4]=(H[i]>>>24)&0xff; Module.HEAPU8[out_hash+i*4+1]=(H[i]>>>16)&0xff; Module.HEAPU8[out_hash+i*4+2]=(H[i]>>>8)&0xff; Module.HEAPU8[out_hash+i*4+3]=H[i]&0xff; }
    },
    // --- OAuth popup stub (for WASM extensions doing PKCE) ---
    // Open a URL on the main thread (popup), block until auth code comes back via SAB.
    // Returns malloc'd string (auth code) or 0 on failure. Caller frees.
    // SAB layout: [flag:Int32][dataLen:Int32][data:UTF-8]  flag: 0=waiting, 1=ready, -1=error
    duckdb_wasm_open_auth_url__sig: 'pi',
    duckdb_wasm_open_auth_url: function (url_ptr) {
        var url = UTF8ToString(url_ptr);
        if (typeof oauthInt32 === 'undefined' || !oauthInt32) return 0;
        Atomics.store(oauthInt32, 0, 0);
        postMessage({ type: 'open-auth-url', url: url });
        Atomics.wait(oauthInt32, 0, 0);
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
