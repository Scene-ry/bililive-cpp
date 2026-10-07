// http.h - WinHTTP 封装（GET/POST、原始响应头、Set-Cookie 可见）
#pragma once

#include <string>
#include <vector>
#include <windows.h>

struct HttpHeader {
    std::wstring name;
    std::wstring value;
};

struct HttpResult {
    bool transportOk = false;   // 传输层成功（拿到了 HTTP 响应）
    DWORD statusCode = 0;
    std::string body;           // 原始响应体（UTF-8）
    std::string rawHeaders;     // 原始响应头（CRLF），供解析 Set-Cookie
    std::string errorMessage;   // transportOk == false 时的错误描述
};

class HttpClient {
public:
    // 发起 HTTPS 请求。postBody 为 nullptr 时发 GET，否则发 POST。
    static HttpResult Request(const std::wstring& method,
                              const std::wstring& host,
                              const std::wstring& path,
                              const std::vector<HttpHeader>& headers,
                              const std::string* postBody,
                              const std::wstring& userAgent);
};
