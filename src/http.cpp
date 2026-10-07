// http.cpp - WinHTTP 封装实现
#include "http.h"
#include "util.h"

#include <winhttp.h>

#include <vector>

namespace {

struct WinHttpHandles {
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;
    ~WinHttpHandles() {
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
    }
};

std::string FormatWinError(DWORD err) {
    LPWSTR buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                             (LPWSTR)&buf, 0, nullptr);
    std::string out;
    if (n && buf) {
        std::wstring w(buf, n);
        while (!w.empty() &&
               (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ' || w.back() == L'\t')) {
            w.pop_back();
        }
        out = util::WideToUtf8(w);
    }
    if (buf) LocalFree(buf);
    if (out.empty()) out = "WinHTTP error " + std::to_string(err);
    return out;
}

}  // namespace

HttpResult HttpClient::Request(const std::wstring& method,
                               const std::wstring& host,
                               const std::wstring& path,
                               const std::vector<HttpHeader>& headers,
                               const std::string* postBody,
                               const std::wstring& userAgent) {
    HttpResult r;
    WinHttpHandles h;

    h.session = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.session) {
        r.errorMessage = "WinHttpOpen: " + FormatWinError(GetLastError());
        return r;
    }
    // 解析 / 连接 / 发送 / 接收 超时（毫秒）
    WinHttpSetTimeouts(h.session, 10000, 10000, 15000, 20000);

    // 明确开启 TLS 1.2（及可用的 TLS 1.3），保证老系统也可用
    {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        WinHttpSetOption(h.session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }

    h.connect = WinHttpConnect(h.session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!h.connect) {
        r.errorMessage = "WinHttpConnect: " + FormatWinError(GetLastError());
        return r;
    }

    h.request = WinHttpOpenRequest(h.connect, method.c_str(), path.c_str(), nullptr,
                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!h.request) {
        r.errorMessage = "WinHttpOpenRequest: " + FormatWinError(GetLastError());
        return r;
    }

#ifdef WINHTTP_OPTION_REDIRECT_POLICY
    {
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(h.request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    }
#endif

    for (const HttpHeader& hd : headers) {
        std::wstring line = hd.name + L": " + hd.value;
        WinHttpAddRequestHeaders(h.request, line.c_str(), (ULONG)-1L,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    BOOL sent = FALSE;
    if (postBody) {
        sent = WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  (LPVOID)postBody->data(), (DWORD)postBody->size(),
                                  (DWORD)postBody->size(), 0);
    } else {
        sent = WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    }
    if (!sent) {
        r.errorMessage = "WinHttpSendRequest: " + FormatWinError(GetLastError());
        return r;
    }

    if (!WinHttpReceiveResponse(h.request, nullptr)) {
        r.errorMessage = "WinHttpReceiveResponse: " + FormatWinError(GetLastError());
        return r;
    }

    DWORD status = 0;
    DWORD len = sizeof(status);
    if (!WinHttpQueryHeaders(h.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX)) {
        r.errorMessage = "WinHttpQueryHeaders(status): " + FormatWinError(GetLastError());
        return r;
    }
    r.statusCode = status;

    // 原始响应头（用于解析多个 Set-Cookie）
    {
        DWORD size = 0;
        WinHttpQueryHeaders(h.request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                            nullptr, &size, WINHTTP_NO_HEADER_INDEX);
        if (size > 0) {
            std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, L'\0');
            if (WinHttpQueryHeaders(h.request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &size,
                                    WINHTTP_NO_HEADER_INDEX)) {
                buf.back() = L'\0';
                r.rawHeaders = util::WideToUtf8(std::wstring(buf.data()));
            }
        }
    }

    // 读取响应体
    std::vector<char> chunk(8192);
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(h.request, chunk.data(), (DWORD)chunk.size(), &read)) {
            r.errorMessage = "WinHttpReadData: " + FormatWinError(GetLastError());
            return r;
        }
        if (read == 0) break;
        r.body.append(chunk.data(), read);
    }

    r.transportOk = true;
    return r;
}
