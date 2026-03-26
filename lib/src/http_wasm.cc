#include "duckdb/web/http_wasm.h"

#include <emscripten.h>

#include <iostream>

#include "duckdb/common/http_util.hpp"
#include "duckdb/web/config.h"

namespace duckdb {
class HTTPLogger;
class FileOpener;
struct FileOpenerInfo;
class HTTPState;
HTTPHeaders TransformHeadersWasm(const HTTPHeaders &header_map, const HTTPParams &params) {
    auto &httpfs_params = params.Cast<HTTPFSParams>();

    HTTPHeaders res_headers;
    for (auto &header : header_map) {
        res_headers.Insert(header.first, header.second);
    }
    if (!httpfs_params.pre_merged_headers) {
        for (auto &entry : params.extra_headers) {
            res_headers.Insert(entry.first, entry.second);
        }
    }
    return res_headers;
}

// Parse the response buffer returned from EM_ASM_PTR.
// Layout: [status:2bytes LE][headersLen:4bytes LE][headers][bodyLen:4bytes LE][body]
static unique_ptr<HTTPResponse> ParseWasmResponse(char *exe) {
    if (!exe) {
        auto res = make_uniq<HTTPResponse>(HTTPStatusCode::NotFound_404);
        res->reason = "XMLHttpRequest failed or unavailable — check the browser console for CORS or network errors";
        return res;
    }
    auto p = reinterpret_cast<uint8_t *>(exe);
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
    free(exe);
    return res;
}

class HTTPWasmClient : public HTTPClient {
   public:
    HTTPWasmClient(HTTPFSParams &http_params, const string &proto_host_port) { host_port = proto_host_port; }
    void Initialize(HTTPParams &params) override {}
    string host_port;

    unique_ptr<HTTPResponse> Get(GetRequestInfo &info) override {
        // clang-format off
        unique_ptr<HTTPResponse> res;

        string path = info.url;
        if (path[0] == '/') path = host_port + info.url;

        if (!web::experimental_s3_tables_global_proxy.empty()) {
            if (info.url.rfind(web::experimental_s3_tables_global_proxy, 0) != 0) {
                auto id_table = path.find("--table-s3.s3.");
                auto id_aws = path.find(".amazonaws.com/");
                if (id_table != std::string::npos && id_aws != std::string::npos && id_table < id_aws) {
                    path = web::experimental_s3_tables_global_proxy + path.substr(8);
                }
            }
        }
        if ((path.rfind("https://", 0) != 0) && (path.rfind("http://", 0) != 0)) {
            path = "https://" + path;
        }
        auto headers = TransformHeadersWasm(info.headers, info.params);

        int n = 0;
        for (auto h : headers) {
            n++;
        }
        char **z = (char **)(void *)malloc(n * 4 * 2);

        int i = 0;
        for (auto h : headers) {
            z[i] = (char *)malloc(h.first.size() * 4 + 1);
            memset(z[i], 0, h.first.size() * 4 + 1);
            memcpy(z[i], h.first.c_str(), h.first.size());
            i++;
            z[i] = (char *)malloc(h.second.size() * 4 + 1);
            memset(z[i], 0, h.second.size() * 4 + 1);
            memcpy(z[i], h.second.c_str(), h.second.size());
            i++;
        }

        char *exe = NULL;
        exe = (char *)EM_ASM_PTR(
            {
                var url = (UTF8ToString($0));
                if (typeof XMLHttpRequest === "undefined") {
                    return 0;
                }
                const xhr = new XMLHttpRequest();
		if (false && url.startsWith("http://")) {
			url = "https://" + url.substr(7);
		}
                xhr.open(UTF8ToString($3), url, false);
                xhr.responseType = "arraybuffer";

                var i = 0;
                var len = $1;
                while (i < len*2) {
                    var ptr1 = HEAP32[($2)/4 + i ];
                    var ptr2 = HEAP32[($2)/4 + i + 1];

                    try {
			var z = encodeURI(UTF8ToString(ptr1));
			if (z === "Host") z = "X-Host-Override";
			if (z === "User-Agent") z = "X-user-agent";
			if (z === "Authorization") {
                        	xhr.setRequestHeader(z, UTF8ToString(ptr2));
			} else {
				
                        	xhr.setRequestHeader(z, encodeURI(UTF8ToString(ptr2)));
			}
                    } catch (error) {
                console.warn("Error while performing XMLHttpRequest.setRequestHeader()", error);
                    }
                    i += 2;
                }

                try {
                    xhr.send(null);
                } catch {
                    return 0;
                }
                var uInt8Array = xhr.response;
                var bodyLen = uInt8Array ? uInt8Array.byteLength : 0;
                var status = xhr.status;
                if (status === 0) return 0;
                var hdrs = xhr.getAllResponseHeaders() || "";
                var hdrsBytes = new TextEncoder().encode(hdrs);
                var hdrsLen = hdrsBytes.length;
                var total = 2 + 4 + hdrsLen + 4 + bodyLen;
                var buf = _malloc(total);
                if (buf === 0) return 0;
                var off = buf;
                Module.HEAPU8[off] = status & 0xFF;
                Module.HEAPU8[off + 1] = (status >> 8) & 0xFF;
                off += 2;
                Module.HEAPU8[off] = hdrsLen & 0xFF;
                Module.HEAPU8[off + 1] = (hdrsLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (hdrsLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (hdrsLen >> 24) & 0xFF;
                off += 4;
                if (hdrsLen > 0) Module.HEAPU8.set(hdrsBytes, off);
                off += hdrsLen;
                Module.HEAPU8[off] = bodyLen & 0xFF;
                Module.HEAPU8[off + 1] = (bodyLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (bodyLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (bodyLen >> 24) & 0xFF;
                off += 4;
                if (bodyLen > 0) {
                    Module.HEAPU8.set(new Uint8Array(uInt8Array), off);
                }
                return buf;
            },
            path.c_str(), n, z, "GET");
        // clang-format on

        i = 0;
        for (auto h : headers) {
            free(z[i]);
            i++;
            free(z[i]);
            i++;
        }
        free(z);

        res = ParseWasmResponse(exe);
        if (res->status == HTTPStatusCode::OK_200 && info.content_handler && !res->body.empty()) {
            info.content_handler(reinterpret_cast<const unsigned char *>(res->body.data()), res->body.size());
        }

        return res;
    }
    unique_ptr<HTTPResponse> Head(HeadRequestInfo &info) override {
        unique_ptr<HTTPResponse> res;

        string path = info.url;
        if (path[0] == '/') path = host_port + info.url;

        if (!web::experimental_s3_tables_global_proxy.empty()) {
            if (info.url.rfind(web::experimental_s3_tables_global_proxy, 0) != 0) {
                auto id_table = path.find("--table-s3.s3.");
                auto id_aws = path.find(".amazonaws.com/");
                if (id_table != std::string::npos && id_aws != std::string::npos && id_table < id_aws) {
                    path = web::experimental_s3_tables_global_proxy + path.substr(8);
                }
            }
        }
        if ((path.rfind("https://", 0) != 0) && (path.rfind("http://", 0) != 0)) {
            path = "https://" + path;
        }
        auto headers = TransformHeadersWasm(info.headers, info.params);
        int n = 0;
        for (auto h : headers) {
            n++;
        }

        char **z = (char **)(void *)malloc(n * 4 * 2);

        int i = 0;
        for (auto h : headers) {
            z[i] = (char *)malloc(h.first.size() * 4 + 1);
            memset(z[i], 0, h.first.size() * 4 + 1);
            memcpy(z[i], h.first.c_str(), h.first.size());
            i++;
            z[i] = (char *)malloc(h.second.size() * 4 + 1);
            memset(z[i], 0, h.second.size() * 4 + 1);
            memcpy(z[i], h.second.c_str(), h.second.size());
            i++;
        }

        // clang-format off
        char *exe = NULL;
        exe = (char *)EM_ASM_PTR(
            {
                var url = (UTF8ToString($0));
                if (typeof XMLHttpRequest === "undefined") {
                    return 0;
                }
                const xhr = new XMLHttpRequest();
		if (false && url.startsWith("http://")) {
			url = "https://" + url.substr(7);
		}
                xhr.open(UTF8ToString($3), url, false);
                xhr.responseType = "arraybuffer";

                var i = 0;
                var len = $1;
                while (i < len*2) {
                    var ptr1 = HEAP32[($2)/4 + i ];
                    var ptr2 = HEAP32[($2)/4 + i + 1];

console.log('HEAD', UTF8ToString(ptr1), UTF8ToString(ptr2));
                    try {
			var z = encodeURI(UTF8ToString(ptr1));
			if (z === "Host") z = "X-Host-Override";
			if (z === "User-Agent") z = "X-user-agent";
			if (z === "Authorization") {
                        	xhr.setRequestHeader(z, UTF8ToString(ptr2));
			} else {
				
                        	xhr.setRequestHeader(z, encodeURI(UTF8ToString(ptr2)));
			}
                    } catch (error) {
                console.warn("Error while performing XMLHttpRequest.setRequestHeader()", error);
                    }
                    i += 2;
                }

                try {
                    xhr.send(null);
                } catch {
                    return 0;
                }
                var uInt8Array = xhr.response;
                var bodyLen = uInt8Array ? uInt8Array.byteLength : 0;
                var status = xhr.status;
                if (status === 0) return 0;
                var hdrs = xhr.getAllResponseHeaders() || "";
                var hdrsBytes = new TextEncoder().encode(hdrs);
                var hdrsLen = hdrsBytes.length;
                var total = 2 + 4 + hdrsLen + 4 + bodyLen;
                var buf = _malloc(total);
                if (buf === 0) return 0;
                var off = buf;
                Module.HEAPU8[off] = status & 0xFF;
                Module.HEAPU8[off + 1] = (status >> 8) & 0xFF;
                off += 2;
                Module.HEAPU8[off] = hdrsLen & 0xFF;
                Module.HEAPU8[off + 1] = (hdrsLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (hdrsLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (hdrsLen >> 24) & 0xFF;
                off += 4;
                if (hdrsLen > 0) Module.HEAPU8.set(hdrsBytes, off);
                off += hdrsLen;
                Module.HEAPU8[off] = bodyLen & 0xFF;
                Module.HEAPU8[off + 1] = (bodyLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (bodyLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (bodyLen >> 24) & 0xFF;
                off += 4;
                if (bodyLen > 0) {
                    Module.HEAPU8.set(new Uint8Array(uInt8Array), off);
                }
                return buf;
            },
            path.c_str(), n, z, "HEAD");

        i = 0;

        for (auto h : headers) {
            free(z[i]);
            i++;
            free(z[i]);
            i++;
        }
        free(z);

        res = ParseWasmResponse(exe);
        return res;
    }
    unique_ptr<HTTPResponse> Post(PostRequestInfo &info) override {
        unique_ptr<HTTPResponse> res;

        string path = info.url;
        if (path[0] == '/') path = host_port + info.url;

        if (!web::experimental_s3_tables_global_proxy.empty()) {
            if (info.url.rfind(web::experimental_s3_tables_global_proxy, 0) != 0) {
                auto id_table = path.find("--table-s3.s3.");
                auto id_aws = path.find(".amazonaws.com/");
                if (id_table != std::string::npos && id_aws != std::string::npos && id_table < id_aws) {
                    path = web::experimental_s3_tables_global_proxy + path.substr(8);
                }
            }
        }
        if ((path.rfind("https://", 0) != 0) && (path.rfind("http://", 0) != 0)) {
            path = "https://" + path;
        }
        auto headers = TransformHeadersWasm(info.headers, info.params);
        int n = 0;
        for (auto h : headers) {
            n++;
        }

        char **z = (char **)(void *)malloc(n * 4 * 2);

        int i = 0;
        for (auto h : headers) {
            z[i] = (char *)malloc(h.first.size() * 4 + 1);
            memset(z[i], 0, h.first.size() * 4 + 1);
            memcpy(z[i], h.first.c_str(), h.first.size());
            i++;
            z[i] = (char *)malloc(h.second.size() * 4 + 1);
            memset(z[i], 0, h.second.size() * 4 + 1);
            memcpy(z[i], h.second.c_str(), h.second.size());
            i++;
        }

        const int buffer_length = info.buffer_in_len;
        char *payload = (char *)malloc(buffer_length);
        memcpy(payload, info.buffer_in, buffer_length);

        // clang-format off
        char *exe = NULL;
        exe = (char *)EM_ASM_PTR(
            {
                var url = (UTF8ToString($0));
                if (typeof XMLHttpRequest === "undefined") {
                    return 0;
                }
                const xhr = new XMLHttpRequest();
		if (false && url.startsWith("http://")) {
			url = "https://" + url.substr(7);
		}
                xhr.open(UTF8ToString($3), url, false);
                xhr.responseType = "arraybuffer";

                var i = 0;
                var len = $1;
                while (i < len*2) {
                    var ptr1 = HEAP32[($2)/4 + i ];
                    var ptr2 = HEAP32[($2)/4 + i + 1];

                    try {
			var z = encodeURI(UTF8ToString(ptr1));
			if (z === "Host") z = "X-Host-Override";
			if (z === "User-Agent") z = "X-user-agent";
			if (z === "Authorization") {
                        	xhr.setRequestHeader(z, UTF8ToString(ptr2));
			} else {
				
                        	xhr.setRequestHeader(z, encodeURI(UTF8ToString(ptr2)));
			}
                    } catch (error) {
                console.warn("Error while performing XMLHttpRequest.setRequestHeader()", error);
                    }
                    i += 2;
                }

//xhr.setRequestHeader("Content-Type", "application/octet-stream");
//xhr.setRequestHeader("Content-Type", "text/json");
                try {
			var post_payload = new Uint8Array($5);

			for (var iii = 0; iii < $5; iii++) {
				post_payload[iii] = Module.HEAPU8[iii + $4];
			}
			xhr.send(post_payload);
                } catch {
                    return 0;
                }
                var uInt8Array = xhr.response;
                var bodyLen = uInt8Array ? uInt8Array.byteLength : 0;
                var status = xhr.status;
                if (status === 0) return 0;
                var hdrs = xhr.getAllResponseHeaders() || "";
                var hdrsBytes = new TextEncoder().encode(hdrs);
                var hdrsLen = hdrsBytes.length;
                var total = 2 + 4 + hdrsLen + 4 + bodyLen;
                var buf = _malloc(total);
                if (buf === 0) return 0;
                var off = buf;
                Module.HEAPU8[off] = status & 0xFF;
                Module.HEAPU8[off + 1] = (status >> 8) & 0xFF;
                off += 2;
                Module.HEAPU8[off] = hdrsLen & 0xFF;
                Module.HEAPU8[off + 1] = (hdrsLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (hdrsLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (hdrsLen >> 24) & 0xFF;
                off += 4;
                if (hdrsLen > 0) Module.HEAPU8.set(hdrsBytes, off);
                off += hdrsLen;
                Module.HEAPU8[off] = bodyLen & 0xFF;
                Module.HEAPU8[off + 1] = (bodyLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (bodyLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (bodyLen >> 24) & 0xFF;
                off += 4;
                if (bodyLen > 0) {
                    Module.HEAPU8.set(new Uint8Array(uInt8Array), off);
                }
                return buf;
            },
            path.c_str(), n, z, "POST", payload, buffer_length);
        // clang-format on

        free(payload);

        i = 0;
        for (auto h : headers) {
            free(z[i]);
            i++;
            free(z[i]);
            i++;
        }
        free(z);

        res = ParseWasmResponse(exe);
        if (!res->body.empty()) {
            info.buffer_out += res->body;
        }
        return res;
    }
    unique_ptr<HTTPResponse> Put(PutRequestInfo &info) override {
        unique_ptr<HTTPResponse> res;

        string path = info.url;
        if (path[0] == '/') path = host_port + info.url;

        if (!web::experimental_s3_tables_global_proxy.empty()) {
            if (info.url.rfind(web::experimental_s3_tables_global_proxy, 0) != 0) {
                auto id_table = path.find("--table-s3.s3.");
                auto id_aws = path.find(".amazonaws.com/");
                if (id_table != std::string::npos && id_aws != std::string::npos && id_table < id_aws) {
                    path = web::experimental_s3_tables_global_proxy + path.substr(8);
                }
            }
        }
        if ((path.rfind("https://", 0) != 0) && (path.rfind("http://", 0) != 0)) {
            path = "https://" + path;
        }

        auto headers = TransformHeadersWasm(info.headers, info.params);
        int n = 0;
        for (auto h : headers) {
            n++;
        }

        char **z = (char **)(void *)malloc(n * 4 * 2);

        int i = 0;
        for (auto h : headers) {
            z[i] = (char *)malloc(h.first.size() * 4 + 1);
            memset(z[i], 0, h.first.size() * 4 + 1);
            memcpy(z[i], h.first.c_str(), h.first.size());
            i++;
            z[i] = (char *)malloc(h.second.size() * 4 + 1);
            memset(z[i], 0, h.second.size() * 4 + 1);
            memcpy(z[i], h.second.c_str(), h.second.size());
            i++;
        }

        const int buffer_length = info.buffer_in_len;
        char *payload = (char *)malloc(buffer_length);
        memcpy(payload, info.buffer_in, buffer_length);

        // clang-format off
        char *exe = NULL;
        exe = (char *)EM_ASM_PTR(
            {
                var url = (UTF8ToString($0));
                if (typeof XMLHttpRequest === "undefined") {
                    return 0;
                }
                const xhr = new XMLHttpRequest();
		if (false && url.startsWith("http://")) {
			url = "https://" + url.substr(7);
		}
                xhr.open(UTF8ToString($3), url, false);
                xhr.responseType = "arraybuffer";

                var i = 0;
                var len = $1;
                while (i < len*2) {
                    var ptr1 = HEAP32[($2)/4 + i ];
                    var ptr2 = HEAP32[($2)/4 + i + 1];

                    try {
			var z = encodeURI(UTF8ToString(ptr1));
			if (z === "Host") z = "X-Host-Override";
			if (z === "User-Agent") z = "X-user-agent";
			if (z === "Authorization") {
                        	xhr.setRequestHeader(z, UTF8ToString(ptr2));
			} else {
				
                        	xhr.setRequestHeader(z, encodeURI(UTF8ToString(ptr2)));
			}
                    } catch (error) {
                console.warn("Error while performing XMLHttpRequest.setRequestHeader()", error);
                    }
                    i += 2;
                }

//xhr.setRequestHeader("Content-Type", "application/octet-stream");
//xhr.setRequestHeader("Content-Type", "text/json");
                try {
			var post_payload = new Uint8Array($5);

			for (var iii = 0; iii < $5; iii++) {
				post_payload[iii] = Module.HEAPU8[iii + $4];
			}
			xhr.send(post_payload);
                } catch {
                    return 0;
                }
                var uInt8Array = xhr.response;
                var bodyLen = uInt8Array ? uInt8Array.byteLength : 0;
                var status = xhr.status;
                if (status === 0) return 0;
                var hdrs = xhr.getAllResponseHeaders() || "";
                var hdrsBytes = new TextEncoder().encode(hdrs);
                var hdrsLen = hdrsBytes.length;
                var total = 2 + 4 + hdrsLen + 4 + bodyLen;
                var buf = _malloc(total);
                if (buf === 0) return 0;
                var off = buf;
                Module.HEAPU8[off] = status & 0xFF;
                Module.HEAPU8[off + 1] = (status >> 8) & 0xFF;
                off += 2;
                Module.HEAPU8[off] = hdrsLen & 0xFF;
                Module.HEAPU8[off + 1] = (hdrsLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (hdrsLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (hdrsLen >> 24) & 0xFF;
                off += 4;
                if (hdrsLen > 0) Module.HEAPU8.set(hdrsBytes, off);
                off += hdrsLen;
                Module.HEAPU8[off] = bodyLen & 0xFF;
                Module.HEAPU8[off + 1] = (bodyLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (bodyLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (bodyLen >> 24) & 0xFF;
                off += 4;
                if (bodyLen > 0) {
                    Module.HEAPU8.set(new Uint8Array(uInt8Array), off);
                }
                return buf;
            },
            path.c_str(), n, z, "PUT", payload, buffer_length);
        // clang-format on

        free(payload);

        i = 0;
        for (auto h : headers) {
            free(z[i]);
            i++;
            free(z[i]);
            i++;
        }
        free(z);

        res = ParseWasmResponse(exe);
        return res;
    }
    unique_ptr<HTTPResponse> Delete(DeleteRequestInfo &info) override {
        unique_ptr<HTTPResponse> res;

        string path = info.url;
        if (path[0] == '/') path = host_port + info.url;

        if (!web::experimental_s3_tables_global_proxy.empty()) {
            if (info.url.rfind(web::experimental_s3_tables_global_proxy, 0) != 0) {
                auto id_table = path.find("--table-s3.s3.");
                auto id_aws = path.find(".amazonaws.com/");
                if (id_table != std::string::npos && id_aws != std::string::npos && id_table < id_aws) {
                    path = web::experimental_s3_tables_global_proxy + path.substr(8);
                }
            }
        }
        if ((path.rfind("https://", 0) != 0) && (path.rfind("http://", 0) != 0)) {
            path = "https://" + path;
        }

        auto headers = TransformHeadersWasm(info.headers, info.params);
        int n = 0;
        for (auto h : headers) {
            n++;
        }

        char **z = (char **)(void *)malloc(n * 4 * 2);

        int i = 0;
        for (auto h : headers) {
            z[i] = (char *)malloc(h.first.size() * 4 + 1);
            memset(z[i], 0, h.first.size() * 4 + 1);
            memcpy(z[i], h.first.c_str(), h.first.size());
            i++;
            z[i] = (char *)malloc(h.second.size() * 4 + 1);
            memset(z[i], 0, h.second.size() * 4 + 1);
            memcpy(z[i], h.second.c_str(), h.second.size());
            i++;
        }

        // clang-format off
        char *exe = NULL;
        exe = (char *)EM_ASM_PTR(
            {
                var url = (UTF8ToString($0));
                if (typeof XMLHttpRequest === "undefined") {
                    return 0;
                }
                const xhr = new XMLHttpRequest();
		if (false && url.startsWith("http://")) {
			url = "https://" + url.substr(7);
		}
                xhr.open(UTF8ToString($3), url, false);
                xhr.responseType = "arraybuffer";

                var i = 0;
                var len = $1;
                while (i < len*2) {
                    var ptr1 = HEAP32[($2)/4 + i ];
                    var ptr2 = HEAP32[($2)/4 + i + 1];

                    try {
			var z = encodeURI(UTF8ToString(ptr1));
			if (z === "Host") z = "X-Host-Override";
			if (z === "User-Agent") z = "X-user-agent";
			if (z === "Authorization") {
                        	xhr.setRequestHeader(z, UTF8ToString(ptr2));
			} else {
				
                        	xhr.setRequestHeader(z, encodeURI(UTF8ToString(ptr2)));
			}
                    } catch (error) {
                console.warn("Error while performing XMLHttpRequest.setRequestHeader()", error);
                    }
                    i += 2;
                }

                try {
                    xhr.send(null);
                } catch {
                    return 0;
                }
                var uInt8Array = xhr.response;
                var bodyLen = uInt8Array ? uInt8Array.byteLength : 0;
                var status = xhr.status;
                if (status === 0) return 0;
                var hdrs = xhr.getAllResponseHeaders() || "";
                var hdrsBytes = new TextEncoder().encode(hdrs);
                var hdrsLen = hdrsBytes.length;
                var total = 2 + 4 + hdrsLen + 4 + bodyLen;
                var buf = _malloc(total);
                if (buf === 0) return 0;
                var off = buf;
                Module.HEAPU8[off] = status & 0xFF;
                Module.HEAPU8[off + 1] = (status >> 8) & 0xFF;
                off += 2;
                Module.HEAPU8[off] = hdrsLen & 0xFF;
                Module.HEAPU8[off + 1] = (hdrsLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (hdrsLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (hdrsLen >> 24) & 0xFF;
                off += 4;
                if (hdrsLen > 0) Module.HEAPU8.set(hdrsBytes, off);
                off += hdrsLen;
                Module.HEAPU8[off] = bodyLen & 0xFF;
                Module.HEAPU8[off + 1] = (bodyLen >> 8) & 0xFF;
                Module.HEAPU8[off + 2] = (bodyLen >> 16) & 0xFF;
                Module.HEAPU8[off + 3] = (bodyLen >> 24) & 0xFF;
                off += 4;
                if (bodyLen > 0) {
                    Module.HEAPU8.set(new Uint8Array(uInt8Array), off);
                }
                return buf;
            },
            path.c_str(), n, z, "DELETE");
        // clang-format on

        i = 0;
        for (auto h : headers) {
            free(z[i]);
            i++;
            free(z[i]);
            i++;
        }
        free(z);

        res = ParseWasmResponse(exe);
        return res;
    }

   private:
    optional_ptr<HTTPState> state;
};

unique_ptr<HTTPClient> HTTPWasmUtil::InitializeClient(HTTPParams &http_params, const string &proto_host_port) {
    auto client = make_uniq<HTTPWasmClient>(http_params.Cast<HTTPFSParams>(), proto_host_port);
    return std::move(client);
}

string HTTPWasmUtil::GetName() const { return "WasmHTTPUtils"; }

}  // namespace duckdb

