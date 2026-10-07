// session.h - 本地登录会话（DPAPI 加密）存储 / 加载 / 安全销毁
//
// 安全模型：
//  * 唯一落盘位置：<exe 目录>\bili_session.bin（无注册表 / 无 AppData / 无临时文件残留）
//  * 内容使用 Windows DPAPI（CryptProtectData）加密，绑定当前 Windows 用户 + 程序内 entropy；
//    文件被复制到其它用户 / 其它机器后无法解密。
//  * 文件损坏 / 被篡改 / 校验失败时立即覆写销毁，避免残留文件被再利用。
#pragma once

#include <string>
#include <cstdint>

struct SessionData {
    std::string uid;             // DedeUserID
    std::string cookie;          // "k=v; k2=v2"
    int64_t savedAt = 0;         // unix 秒
};

namespace session {

// 会话文件名（与主程序同目录）
extern const wchar_t* kFileName;

// 会话文件是否存在于 exe 同目录
bool Exists();

// 加载并解密会话。
//  - 返回 true：会话有效（已通过内容校验）
//  - 返回 false：fileExisted 指示文件是否曾经存在（存在但无效时已被覆写销毁）
bool Load(SessionData& out, bool& fileExisted);

// 加密并保存会话（先写临时文件再原子替换）。失败返回 false。
bool Save(const SessionData& data);

// 校验会话内容：uid 与 cookie 一致，且关键 cookie 完整
bool Validate(const SessionData& data);

// 安全销毁：随机数据覆写 + 删除（连带清理临时文件）
void Wipe();

}  // namespace session
