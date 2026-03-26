// DuckDB-WASM Worker - runs DuckDB in a Web Worker where sync XHR is allowed
importScripts('./packages/duckdb-wasm/src/bindings/duckdb-eh.js');

// SharedArrayBuffer for OAuth PKCE — main thread opens popup, auth code flows back here.
// Layout: [flag:Int32][dataLen:Int32][data:UTF-8 string]
// flag: 0=waiting, 1=code ready, -1=auth failed
var oauthSAB = null;
var oauthInt32 = null;
var oauthBytes = null;

function callSRet(mod, funcName, argTypes, args) {
    const sp = mod.stackSave();
    const response = mod.stackAlloc(3 * 8);
    argTypes.unshift('number');
    args.unshift(response);
    mod.ccall(funcName, null, argTypes, args);
    const heap = mod.HEAPF64;
    const status = heap[(response >> 3) + 0];
    const data = heap[(response >> 3) + 1];
    const dataSize = heap[(response >> 3) + 2];
    mod.stackRestore(sp);
    return [status, data, dataSize];
}

function readString(mod, ptr, len) {
    return new TextDecoder().decode(new Uint8Array(mod.HEAPU8.buffer, ptr, len));
}

let module = null;
let connHdl = null;

function runQuery(sql) {
    const [qStatus, qData, qSize] = callSRet(
        module, 'duckdb_web_query_run', ['number', 'string'], [connHdl, sql]
    );
    if (qStatus !== 0 && qSize > 0) {
        return { ok: false, error: readString(module, qData, qSize) };
    }
    return { ok: true, status: qStatus };
}

function runQueryWithResults(sql) {
    const [qStatus, qData, qSize] = callSRet(
        module, 'duckdb_web_query_run', ['number', 'string'], [connHdl, sql]
    );
    if (qStatus !== 0 && qSize > 0) {
        return { ok: false, error: readString(module, qData, qSize) };
    }

    // The query_run response itself contains the Arrow IPC result
    // status=0 means ARROW_BUFFER, qData is the pointer, qSize is the size
    if (qData > 0 && qSize > 0) {
        const arrowBuffer = new Uint8Array(qSize);
        arrowBuffer.set(new Uint8Array(module.HEAPU8.buffer, qData, qSize));
        return { ok: true, arrowBuffers: [arrowBuffer.buffer] };
    }

    return { ok: true };
}

async function init() {
    postMessage({ type: 'log', msg: 'Loading WASM module (MAIN_MODULE)...', cls: 'info' });

    module = await DuckDB({
        locateFile: (path) => './packages/duckdb-wasm/src/bindings/' + path
    });
    postMessage({ type: 'log', msg: 'Module instantiated', cls: 'ok' });

    const config = JSON.stringify({ allowUnsignedExtensions: true });
    const [openStatus, openData, openSize] = callSRet(module, 'duckdb_web_open', ['string'], [config]);
    if (openStatus !== 0) {
        postMessage({ type: 'log', msg: `Open failed: ${openSize > 0 ? readString(module, openData, openSize) : 'unknown'}`, cls: 'err' });
        return;
    }
    postMessage({ type: 'log', msg: 'Database opened (allowUnsignedExtensions=true)', cls: 'ok' });

    connHdl = module.ccall('duckdb_web_connect', 'number', [], []);
    postMessage({ type: 'log', msg: `Connected`, cls: 'ok' });

    runQuery("SET autoinstall_known_extensions=false");
    runQuery("SET autoload_known_extensions=true");
    let r = runQuery("SET custom_extension_repository='http://localhost:8765/extensions'");
    postMessage({ type: 'log', msg: 'Extension repo: http://localhost:8765/extensions', cls: 'ok' });

    postMessage({ type: 'log', msg: '', cls: '' });

    const exts = ['json', 'autocomplete', 'vgi'];
    for (const ext of exts) {
        postMessage({ type: 'log', msg: `Loading ${ext}...`, cls: 'info' });
        r = runQuery(`LOAD 'http://localhost:8765/extensions/v1.5.1/wasm_eh/${ext}.duckdb_extension.wasm'`);
        postMessage({ type: 'log', msg: `LOAD ${ext}  ` + (r.ok ? 'OK' : r.error), cls: r.ok ? 'ok' : 'err' });
    }

    postMessage({ type: 'ready' });
}

onmessage = function(e) {
    if (e.data.type === 'init-oauth-sab') {
        oauthSAB = e.data.sab;
        oauthInt32 = new Int32Array(oauthSAB);
        oauthBytes = new Uint8Array(oauthSAB);
        return;
    }
    if (e.data.type === 'complete') {
        const text = e.data.text;
        const r = runQueryWithResults("CALL sql_auto_complete('" + text.replace(/'/g, "''") + "')");
        if (r.ok && r.arrowBuffers) {
            postMessage({ type: 'completions', arrowBuffers: r.arrowBuffers }, r.arrowBuffers);
        } else {
            postMessage({ type: 'completions', arrowBuffers: null });
        }
        return;
    }
    if (e.data.type === 'query') {
        const sql = e.data.sql;
        const upper = sql.trim().toUpperCase();
        const isStatement = upper.startsWith('SET ') || upper.startsWith('INSTALL ') || upper.startsWith('LOAD ') ||
            upper.startsWith('CREATE ') || upper.startsWith('DROP ') || upper.startsWith('INSERT ') ||
            upper.startsWith('DELETE ') || upper.startsWith('UPDATE ') || upper.startsWith('ALTER ');

        if (isStatement) {
            const r = runQuery(sql);
            postMessage({ type: 'result', ok: r.ok, error: r.error });
        } else {
            const r = runQueryWithResults(sql);
            if (r.arrowBuffers) {
                postMessage({ type: 'result', ok: true, arrowBuffers: r.arrowBuffers }, r.arrowBuffers);
            } else {
                postMessage({ type: 'result', ok: r.ok, error: r.error });
            }
        }
    }
};

init();
