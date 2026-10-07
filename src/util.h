// util.h - 通用工具（UTF 转换 / URL 编码 / 路径 / 安全内存 / 剪贴板 / 时间）
#pragma once

#include <string>
#include <cstdint>

namespace util {

// ---- 字符串编码 ----
std::string WideToUtf8(const std::wstring& s);
std::wstring Utf8ToWide(const std::string& s);

// encodeURIComponent 风格的 URL 编码（输入 UTF-8 字节）
std::string UrlEncode(const std::string& s);

// 简单字符串工具
std::string Trim(const std::string& s);
std::string ToLowerAscii(const std::string& s);
bool StartsWith(const std::string& s, const std::string& prefix);
bool EndsWith(const std::string& s, const std::string& suffix);

// Cookie 工具：从 "k=v; k2=v2" 中取值（找不到返回空串）
std::string CookieValue(const std::string& jar, const std::string& name);

// ---- 路径 ----
// 当前 exe 所在目录（带结尾反斜杠）；失败返回空串
std::wstring ExeDir();
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);

// ---- 安全内存 ----
void SecureZero(void* data, size_t len);
void SecureZeroString(std::string& s);
// 加密学安全随机数（失败返回 false）
bool SecureRandom(void* buf, size_t len);

// ---- 剪贴板 ----
bool CopyTextToClipboard(void* ownerHwnd, const std::wstring& text);

// ---- 时间 ----
int64_t UnixNow();

}  // namespace util
