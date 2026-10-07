// util.cpp - 通用工具实现
#include "util.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <bcrypt.h>

#include <ctime>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace util {

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], len, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], len);
    return out;
}

std::string UrlEncode(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' ||
            c == '(' || c == ')') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::string ToLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return out;
}

bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string CookieValue(const std::string& jar, const std::string& name) {
    size_t pos = 0;
    while (pos <= jar.size()) {
        size_t semi = jar.find(';', pos);
        std::string token = Trim(jar.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos));
        size_t eq = token.find('=');
        if (eq != std::string::npos) {
            std::string key = Trim(token.substr(0, eq));
            if (key == name) return token.substr(eq + 1);
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return std::string();
}

std::wstring ExeDir() {
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return std::wstring();
    std::wstring path(buf, n);
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    return path.substr(0, pos + 1);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    std::wstring out = dir;
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out += name;
    return out;
}

void SecureZero(void* data, size_t len) {
    if (data && len) SecureZeroMemory(data, len);
}

void SecureZeroString(std::string& s) {
    if (!s.empty()) {
        SecureZeroMemory(&s[0], s.size());
        s.clear();
        s.shrink_to_fit();
    }
}

bool SecureRandom(void* buf, size_t len) {
    if (!buf || !len) return true;
    NTSTATUS st = BCryptGenRandom(nullptr, (PUCHAR)buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (st == 0) return true;
    // 退路：不理想但保证功能可用
    unsigned char* p = (unsigned char*)buf;
    for (size_t i = 0; i < len; ++i) p[i] = (unsigned char)(rand() & 0xFF);
    return false;
}

bool CopyTextToClipboard(void* ownerHwnd, const std::wstring& text) {
    HWND owner = (HWND)ownerHwnd;
    if (!OpenClipboard(owner)) return false;
    bool ok = false;
    if (EmptyClipboard()) {
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* p = GlobalLock(h);
            if (p) {
                memcpy(p, text.c_str(), bytes);
                GlobalUnlock(h);
                if (SetClipboardData(CF_UNICODETEXT, h)) {
                    ok = true;  // 所有权已转移给系统
                } else {
                    GlobalFree(h);
                }
            } else {
                GlobalFree(h);
            }
        }
    }
    CloseClipboard();
    return ok;
}

int64_t UnixNow() {
    return (int64_t)time(nullptr);
}

}  // namespace util
