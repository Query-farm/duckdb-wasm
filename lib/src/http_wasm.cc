#include "duckdb/web/http_wasm.h"

#include <emscripten.h>

#include "duckdb/common/http_util.hpp"
#include "duckdb/web/config.h"

// js-stubs that need to be available to WASM side modules (extensions).
// The main module must reference them so the linker includes them in wasmImports.
extern "C" {
void duckdb_wasm_crypto_random(void *buf, int len);
void duckdb_wasm_sha256(const void *data, int len, void *out_hash);
char *duckdb_wasm_open_auth_url(const char *url);
char *duckdb_wasm_get_auth_error(int unused);
}

__attribute__((constructor)) static void _register_wasm_stubs() {
    // Force these js-stubs into wasmImports by calling them once at startup.
    // duckdb_wasm_crypto_random with len=0 is a no-op.
    duckdb_wasm_crypto_random(nullptr, 0);
    // The others just need their address taken to prevent tree-shaking.
    volatile auto p1 = &duckdb_wasm_sha256;
    volatile auto p2 = &duckdb_wasm_open_auth_url;
    volatile auto p3 = &duckdb_wasm_get_auth_error;
    (void)p1; (void)p2; (void)p3;
}

namespace duckdb {
class HTTPLogger;
class FileOpener;
struct FileOpenerInfo;
class HTTPState;

//===--------------------------------------------------------------------===//
// Response parsing
//===--------------------------------------------------------------------===//

// Parse the binary response buffer returned from XHR JS functions.
// Layout: [status:2 LE][headersLen:4 LE][headers (UTF-8)][bodyLen:4 LE][body]
// Takes ownership of `buf` (frees it).
// If `buf` is null (XHR unavailable, CORS block, OOM, or send exception), returns 404 with a diagnostic.
static unique_ptr<HTTPResponse> ParseWasmResponse(char *buf) {
    if (!buf) {
        auto res = make_uniq<HTTPResponse>(HTTPStatusCode::NotFound_404);
        res->reason = "XMLHttpRequest failed or unavailable — check the browser console for CORS or network errors";
        return res;
    }
    auto p = reinterpret_cast<uint8_t *>(buf);
    uint16_t status_code = p[0] | (p[1] << 8);
    p += 2;
    uint32_t headers_len = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    p += 4;
    string raw_headers(reinterpret_cast<char *>(p), headers_len);
    p += headers_len;
    uint32_t body_len = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    p += 4;

    auto res = make_uniq<HTTPResponse>(HTTPUtil::ToStatusCode(status_code));
    if (body_len > 0) {
        res->body = string(reinterpret_cast<char *>(p), body_len);
    }
    for (auto &line : StringUtil::Split(raw_headers, "\r\n")) {
        auto colon = line.find(':');
        if (colon != string::npos) {
            auto key = line.substr(0, colon);
            auto val = line.substr(colon + 1);
            StringUtil::Trim(key);
            StringUtil::Trim(val);
            if (!key.empty()) {
                res->headers.Insert(key, val);
            }
        }
    }
    free(buf);
    return res;
}

//===--------------------------------------------------------------------===//
// URL normalization
//===--------------------------------------------------------------------===//

static string NormalizeUrl(const string &url, const string &host_port) {
    string path = url;
    if (!path.empty() && path[0] == '/') {
        path = host_port + url;
    }
    if (!web::experimental_s3_tables_global_proxy.empty()) {
        if (url.rfind(web::experimental_s3_tables_global_proxy, 0) != 0) {
            auto id_table = path.find("--table-s3.s3.");
            auto id_aws = path.find(".amazonaws.com/");
            if (id_table != std::string::npos && id_aws != std::string::npos && id_table < id_aws) {
                path = web::experimental_s3_tables_global_proxy + path.substr(8);
            }
        }
    }
    if (path.rfind("https://", 0) != 0 && path.rfind("http://", 0) != 0) {
        path = "https://" + path;
    }
    return path;
}

//===--------------------------------------------------------------------===//
// Header marshalling (C++ → WASM heap for JS)
//===--------------------------------------------------------------------===//

struct WasmHeaderArray {
    char **ptrs = nullptr;
    int count = 0;

    WasmHeaderArray(const HTTPHeaders &headers, const HTTPParams &params) {
        auto &httpfs_params = params.Cast<HTTPFSParams>();
        HTTPHeaders merged;
        for (auto &h : headers) {
            merged.Insert(h.first, h.second);
        }
        if (!httpfs_params.pre_merged_headers) {
            for (auto &e : params.extra_headers) {
                merged.Insert(e.first, e.second);
            }
        }
        for (auto &h : merged) {
            count++;
        }
        ptrs = static_cast<char **>(malloc(count * 2 * sizeof(char *)));
        int i = 0;
        for (auto &h : merged) {
            ptrs[i] = static_cast<char *>(malloc(h.first.size() + 1));
            memcpy(ptrs[i], h.first.c_str(), h.first.size() + 1);
            i++;
            ptrs[i] = static_cast<char *>(malloc(h.second.size() + 1));
            memcpy(ptrs[i], h.second.c_str(), h.second.size() + 1);
            i++;
        }
    }
    ~WasmHeaderArray() {
        for (int i = 0; i < count * 2; i++) {
            free(ptrs[i]);
        }
        free(ptrs);
    }
    WasmHeaderArray(const WasmHeaderArray &) = delete;
    WasmHeaderArray &operator=(const WasmHeaderArray &) = delete;
};

//===--------------------------------------------------------------------===//
// EM_JS XHR functions — defined once, called by all HTTP methods.
//
// These are real C functions with JS bodies (via emscripten EM_JS).
// Unlike EM_ASM, they use named parameters and can be shared across call sites.
//
// Response format: [status:2 LE][hdrsLen:4 LE][headers][bodyLen:4 LE][body]
// Returns 0 on: XHR unavailable, send exception, CORS block (status 0), malloc OOM.
//===--------------------------------------------------------------------===//

// clang-format off

// XHR for methods without a request body (GET, HEAD, DELETE).
EM_JS(char*, wasm_xhr_no_body, (const char *url_ptr, int header_count, char **header_array, const char *method_ptr), {
    var url = UTF8ToString(url_ptr);
    if (typeof XMLHttpRequest === 'undefined') return 0;
    var xhr = new XMLHttpRequest();
    xhr.open(UTF8ToString(method_ptr), url, false);
    xhr.responseType = 'arraybuffer';
    var i = 0;
    while (i < header_count * 2) {
        var p1 = HEAP32[(header_array) / 4 + i];
        var p2 = HEAP32[(header_array) / 4 + i + 1];
        try {
            var name = UTF8ToString(p1);
            if (name === 'Host') name = 'X-Host-Override';
            if (name === 'User-Agent') name = 'X-user-agent';
            xhr.setRequestHeader(name, UTF8ToString(p2));
        } catch (e) { console.warn('XHR setRequestHeader failed:', e); }
        i += 2;
    }
    try { xhr.send(null); } catch (e) { console.error('XHR send failed:', e); return 0; }
    var resp = xhr.response;
    var bodyLen = resp ? resp.byteLength : 0;
    var status = xhr.status;
    if (status === 0) return 0;
    var hdrs = xhr.getAllResponseHeaders() || '';
    var hdrsBuf = new TextEncoder().encode(hdrs);
    var hdrsLen = hdrsBuf.length;
    var total = 2 + 4 + hdrsLen + 4 + bodyLen;
    var buf = _malloc(total);
    if (buf === 0) return 0;
    var o = buf;
    Module.HEAPU8[o] = status & 0xFF; Module.HEAPU8[o + 1] = (status >> 8) & 0xFF; o += 2;
    Module.HEAPU8[o] = hdrsLen & 0xFF; Module.HEAPU8[o + 1] = (hdrsLen >> 8) & 0xFF;
    Module.HEAPU8[o + 2] = (hdrsLen >> 16) & 0xFF; Module.HEAPU8[o + 3] = (hdrsLen >> 24) & 0xFF; o += 4;
    if (hdrsLen > 0) Module.HEAPU8.set(hdrsBuf, o); o += hdrsLen;
    Module.HEAPU8[o] = bodyLen & 0xFF; Module.HEAPU8[o + 1] = (bodyLen >> 8) & 0xFF;
    Module.HEAPU8[o + 2] = (bodyLen >> 16) & 0xFF; Module.HEAPU8[o + 3] = (bodyLen >> 24) & 0xFF; o += 4;
    if (bodyLen > 0) Module.HEAPU8.set(new Uint8Array(resp), o);
    return buf;
});

// XHR for methods with a request body (POST, PUT).
EM_JS(char*, wasm_xhr_with_body, (const char *url_ptr, int header_count, char **header_array, const char *method_ptr, const char *payload_ptr, int payload_len), {
    var url = UTF8ToString(url_ptr);
    if (typeof XMLHttpRequest === 'undefined') return 0;
    var xhr = new XMLHttpRequest();
    xhr.open(UTF8ToString(method_ptr), url, false);
    xhr.responseType = 'arraybuffer';
    var i = 0;
    while (i < header_count * 2) {
        var p1 = HEAP32[(header_array) / 4 + i];
        var p2 = HEAP32[(header_array) / 4 + i + 1];
        try {
            var name = UTF8ToString(p1);
            if (name === 'Host') name = 'X-Host-Override';
            if (name === 'User-Agent') name = 'X-user-agent';
            xhr.setRequestHeader(name, UTF8ToString(p2));
        } catch (e) { console.warn('XHR setRequestHeader failed:', e); }
        i += 2;
    }
    try {
        xhr.send(Module.HEAPU8.slice(payload_ptr, payload_ptr + payload_len));
    } catch (e) { console.error('XHR send failed:', e); return 0; }
    var resp = xhr.response;
    var bodyLen = resp ? resp.byteLength : 0;
    var status = xhr.status;
    if (status === 0) return 0;
    var hdrs = xhr.getAllResponseHeaders() || '';
    var hdrsBuf = new TextEncoder().encode(hdrs);
    var hdrsLen = hdrsBuf.length;
    var total = 2 + 4 + hdrsLen + 4 + bodyLen;
    var buf = _malloc(total);
    if (buf === 0) return 0;
    var o = buf;
    Module.HEAPU8[o] = status & 0xFF; Module.HEAPU8[o + 1] = (status >> 8) & 0xFF; o += 2;
    Module.HEAPU8[o] = hdrsLen & 0xFF; Module.HEAPU8[o + 1] = (hdrsLen >> 8) & 0xFF;
    Module.HEAPU8[o + 2] = (hdrsLen >> 16) & 0xFF; Module.HEAPU8[o + 3] = (hdrsLen >> 24) & 0xFF; o += 4;
    if (hdrsLen > 0) Module.HEAPU8.set(hdrsBuf, o); o += hdrsLen;
    Module.HEAPU8[o] = bodyLen & 0xFF; Module.HEAPU8[o + 1] = (bodyLen >> 8) & 0xFF;
    Module.HEAPU8[o + 2] = (bodyLen >> 16) & 0xFF; Module.HEAPU8[o + 3] = (bodyLen >> 24) & 0xFF; o += 4;
    if (bodyLen > 0) Module.HEAPU8.set(new Uint8Array(resp), o);
    return buf;
});

// clang-format on

//===--------------------------------------------------------------------===//
// HTTPWasmClient — thin wrappers that normalize URL, marshal headers,
// dispatch to the shared EM_JS XHR functions, and parse the response.
//===--------------------------------------------------------------------===//

class HTTPWasmClient : public HTTPClient {
   public:
    HTTPWasmClient(HTTPFSParams &http_params, const string &proto_host_port) : host_port(proto_host_port) {}
    void Initialize(HTTPParams &params) override {}
    string host_port;

    unique_ptr<HTTPResponse> Get(GetRequestInfo &info) override {
        auto path = NormalizeUrl(info.url, host_port);
        WasmHeaderArray h(info.headers, info.params);
        auto exe = wasm_xhr_no_body(path.c_str(), h.count, h.ptrs, "GET");
        auto res = ParseWasmResponse(exe);
        if (res->status == HTTPStatusCode::OK_200 && info.content_handler && !res->body.empty()) {
            info.content_handler(reinterpret_cast<const unsigned char *>(res->body.data()), res->body.size());
        }
        return res;
    }

    unique_ptr<HTTPResponse> Head(HeadRequestInfo &info) override {
        auto path = NormalizeUrl(info.url, host_port);
        WasmHeaderArray h(info.headers, info.params);
        return ParseWasmResponse(wasm_xhr_no_body(path.c_str(), h.count, h.ptrs, "HEAD"));
    }

    unique_ptr<HTTPResponse> Post(PostRequestInfo &info) override {
        auto path = NormalizeUrl(info.url, host_port);
        WasmHeaderArray h(info.headers, info.params);
        auto exe = wasm_xhr_with_body(path.c_str(), h.count, h.ptrs, "POST",
                                      reinterpret_cast<const char *>(info.buffer_in), info.buffer_in_len);
        auto res = ParseWasmResponse(exe);
        if (!res->body.empty()) {
            info.buffer_out += res->body;
        }
        return res;
    }

    unique_ptr<HTTPResponse> Put(PutRequestInfo &info) override {
        auto path = NormalizeUrl(info.url, host_port);
        WasmHeaderArray h(info.headers, info.params);
        return ParseWasmResponse(wasm_xhr_with_body(path.c_str(), h.count, h.ptrs, "PUT",
                                                    reinterpret_cast<const char *>(info.buffer_in), info.buffer_in_len));
    }

    unique_ptr<HTTPResponse> Delete(DeleteRequestInfo &info) override {
        auto path = NormalizeUrl(info.url, host_port);
        WasmHeaderArray h(info.headers, info.params);
        return ParseWasmResponse(wasm_xhr_no_body(path.c_str(), h.count, h.ptrs, "DELETE"));
    }

   private:
    optional_ptr<HTTPState> state;
};

unique_ptr<HTTPClient> HTTPWasmUtil::InitializeClient(HTTPParams &http_params, const string &proto_host_port) {
    return make_uniq<HTTPWasmClient>(http_params.Cast<HTTPFSParams>(), proto_host_port);
}

string HTTPWasmUtil::GetName() const {
    return "WasmHTTPUtils";
}

}  // namespace duckdb
