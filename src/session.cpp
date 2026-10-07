// session.cpp - DPAPI 会话存储实现
#include "session.h"
#include "util.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <wincrypt.h>

#include <vector>
#include <cstring>

namespace session {

const wchar_t* kFileName = L"bili_session.bin";

namespace {

const char kMagic[4] = {'B', 'I', 'L', 'C'};
const DWORD kVersion = 1;
const size_t kMaxFileSize = 64 * 1024;

// 程序内 entropy：与 Windows 用户凭据共同构成解密条件
const BYTE kEntropy[32] = {
    0x3F, 0xA7, 0x42, 0x9E, 0x51, 0xD2, 0x77, 0xB0, 0x0C, 0x66, 0xE1, 0x38, 0x94, 0x5A, 0x21, 0xC3,
    0x8D, 0x4F, 0x72, 0xBE, 0x19, 0xE5, 0x63, 0x0A, 0xD8, 0x47, 0x92, 0x35, 0xAB, 0x7C, 0x18, 0xFF};

std::wstring FilePath() {
    std::wstring dir = util::ExeDir();
    if (dir.empty()) return std::wstring(kFileName);
    return util::JoinPath(dir, kFileName);
}

std::wstring TmpPath() {
    return FilePath() + L".tmp";
}

void PutU32(std::vector<BYTE>& out, uint32_t v) {
    out.push_back((BYTE)(v & 0xFF));
    out.push_back((BYTE)((v >> 8) & 0xFF));
    out.push_back((BYTE)((v >> 16) & 0xFF));
    out.push_back((BYTE)((v >> 24) & 0xFF));
}

uint32_t GetU32(const BYTE* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool ProtectData(const std::string& plain, std::vector<BYTE>& outBlob) {
    DATA_BLOB in;
    in.pbData = (BYTE*)plain.data();
    in.cbData = (DWORD)plain.size();
    DATA_BLOB entropy;
    entropy.pbData = (BYTE*)kEntropy;
    entropy.cbData = sizeof(kEntropy);
    DATA_BLOB out = {0, nullptr};
    if (!CryptProtectData(&in, L"biliLiveCpp session", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return false;
    }
    outBlob.assign(out.pbData, out.pbData + out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

bool UnprotectData(const std::vector<BYTE>& blob, std::string& plain) {
    DATA_BLOB in;
    in.pbData = (BYTE*)blob.data();
    in.cbData = (DWORD)blob.size();
    DATA_BLOB entropy;
    entropy.pbData = (BYTE*)kEntropy;
    entropy.cbData = sizeof(kEntropy);
    DATA_BLOB out = {0, nullptr};
    if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return false;
    }
    plain.assign((const char*)out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

std::string BuildPlain(const SessionData& d) {
    std::string out;
    out += "uid=";
    out += d.uid;
    out += "\ncookie=";
    out += d.cookie;
    out += "\nts=";
    out += std::to_string(d.savedAt);
    out += "\n";
    return out;
}

bool ParsePlain(const std::string& plain, SessionData& out) {
    out = SessionData();
    size_t pos = 0;
    while (pos < plain.size()) {
        size_t nl = plain.find('\n', pos);
        if (nl == std::string::npos) nl = plain.size();
        std::string line = plain.substr(pos, nl - pos);
        pos = nl + 1;
        if (util::StartsWith(line, "uid=")) {
            out.uid = line.substr(4);
        } else if (util::StartsWith(line, "cookie=")) {
            out.cookie = line.substr(7);
        } else if (util::StartsWith(line, "ts=")) {
            out.savedAt = strtoll(line.c_str() + 3, nullptr, 10);
        }
    }
    return !out.uid.empty() && !out.cookie.empty();
}

}  // namespace

bool Exists() {
    DWORD attr = GetFileAttributesW(FilePath().c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool Validate(const SessionData& d) {
    if (d.uid.empty() || d.cookie.empty()) return false;
    // uid 必须与 cookie 中 DedeUserID 一致（防伪造 / 防拼接残留）
    if (util::CookieValue(d.cookie, "DedeUserID") != d.uid) return false;
    // 关键 cookie 完整性：SESSDATA 用于身份、bili_jct 用于 CSRF
    if (util::CookieValue(d.cookie, "SESSDATA").empty()) return false;
    if (util::CookieValue(d.cookie, "bili_jct").empty()) return false;
    return true;
}

bool Load(SessionData& out, bool& fileExisted) {
    out = SessionData();
    fileExisted = false;

    std::wstring path = FilePath();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        // 文件不存在：正常未登录场景，同时清理可能的遗留临时文件
        DeleteFileW(TmpPath().c_str());
        return false;
    }
    fileExisted = true;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(f, &size) || size.QuadPart < 12 || size.QuadPart > (LONGLONG)kMaxFileSize) {
        CloseHandle(f);
        Wipe();
        return false;
    }
    std::vector<BYTE> buf((size_t)size.QuadPart);
    DWORD read = 0;
    BOOL ok = ReadFile(f, buf.data(), (DWORD)buf.size(), &read, nullptr);
    CloseHandle(f);
    if (!ok || read != buf.size()) {
        Wipe();
        return false;
    }

    if (memcmp(buf.data(), kMagic, 4) != 0) {
        Wipe();
        return false;
    }
    uint32_t version = GetU32(buf.data() + 4);
    uint32_t payloadLen = GetU32(buf.data() + 8);
    if (version != kVersion || payloadLen != buf.size() - 12) {
        // 版本不符或长度异常：销毁，防止旧格式 / 篡改内容被继续使用
        Wipe();
        return false;
    }

    std::vector<BYTE> blob(buf.begin() + 12, buf.end());
    std::string plain;
    if (!UnprotectData(blob, plain)) {
        // 解密失败（换用户 / 换机器 / 篡改）：销毁残留
        Wipe();
        return false;
    }

    bool parsed = ParsePlain(plain, out);
    util::SecureZeroString(plain);
    if (!parsed || !Validate(out)) {
        out = SessionData();
        Wipe();
        return false;
    }
    return true;
}

bool Save(const SessionData& data) {
    if (!Validate(data)) return false;

    std::string plain = BuildPlain(data);
    std::vector<BYTE> blob;
    bool ok = ProtectData(plain, blob);
    util::SecureZeroString(plain);
    if (!ok) return false;

    std::vector<BYTE> file;
    file.reserve(12 + blob.size());
    file.insert(file.end(), kMagic, kMagic + 4);
    PutU32(file, kVersion);
    PutU32(file, (uint32_t)blob.size());
    file.insert(file.end(), blob.begin(), blob.end());

    std::wstring tmp = TmpPath();
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL w = WriteFile(f, file.data(), (DWORD)file.size(), &written, nullptr);
    if (w) FlushFileBuffers(f);
    CloseHandle(f);
    if (!w || written != file.size()) {
        DeleteFileW(tmp.c_str());
        return false;
    }

    std::wstring path = FilePath();
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

void Wipe() {
    // 先清理可能的临时残留
    DeleteFileW(TmpPath().c_str());

    std::wstring path = FilePath();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size = {};
        if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart <= (LONGLONG)kMaxFileSize) {
            std::vector<BYTE> rnd((size_t)size.QuadPart);
            util::SecureRandom(rnd.data(), rnd.size());
            SetFilePointer(f, 0, nullptr, FILE_BEGIN);
            DWORD written = 0;
            WriteFile(f, rnd.data(), (DWORD)rnd.size(), &written, nullptr);
            FlushFileBuffers(f);
        }
        CloseHandle(f);
        DeleteFileW(path.c_str());
    }
}

}  // namespace session
