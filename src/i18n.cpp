// i18n.cpp - 多语言字符串表 / 模板渲染 / 语言设置持久化
//
// 语言设置文件与主程序 exe 同目录（bili_settings.ini），仅保存语言偏好，
// 不含任何凭证信息。
#include "i18n.h"

#include "util.h"

#include <windows.h>

namespace i18n {

namespace {

Lang g_lang = Lang::En;  // 默认英文

// ---------------- 英文表（顺序必须与 i18n.h 中 StrId 完全一致） ----------------
const wchar_t* kEn[] = {
    L"",  // S_NONE

    // 通用 / 入口
    L"bilibili Sign-in (QR Scan)",  // S_APP_TITLE_LOGIN
    L"BiliLive Console",            // S_APP_TITLE_MAIN
    L"Notice",                      // S_MB_INFO_TITLE
    L"Loading QR code…",            // S_QR_LOADING
    L"QR encoding failed (content too long)",  // S_QR_TOO_LONG
    L"Detected local credentials that are invalid, corrupted, or from another user. They have "
    L"been securely erased.\nPlease scan the QR code to sign in again.",  // S_MB_BROKEN_SESSION

    // 凭证销毁提示
    L"No valid local credentials found. Please scan the QR code to sign in again.",  // S_NOTICE_NO_SESSION
    L"Local credentials are invalid or do not match the record, and have been securely erased.\n"
    L"Please scan the QR code to sign in again.",  // S_NOTICE_CRED_INVALID
    L"Signed out. Local credentials have been securely erased.",  // S_NOTICE_LOGGED_OUT
    L"Local credentials have been securely erased.",              // S_NOTICE_LOCAL_WIPED
    L"The server confirmed the session is no longer valid. Local credentials have been securely "
    L"erased.",  // S_NOTICE_SERVER_INVALID

    // 登录窗口
    L"Refresh QR Code",  // S_LOGIN_REFRESH
    L"Credentials are DPAPI-encrypted, stored in this folder;\r\nerased immediately on sign-out.",  // S_LOGIN_HINT
    L"Generating QR code…",  // S_LS_GENERATING
    L"Scan the QR code with the bilibili App to sign in",  // S_LS_SCAN_PROMPT
    L"Failed to generate QR code: %1. Click \"Refresh QR Code\" to retry",  // S_LS_GEN_FAILED
    L"Network error, retrying automatically…",       // S_LS_NET_RETRY
    L"Signed in but credentials are incomplete, refreshing QR code…",  // S_LS_CRED_INCOMPLETE
    L"QR code expired, refreshing…",                 // S_LS_EXPIRED
    L"Scanned. Please confirm sign-in on your phone",  // S_LS_SCANNED
    L"Waiting for scan result (code=%1)…",           // S_LS_WAIT_CODE
    L"Signed in, but saving local credentials failed (the program folder may not be writable).\n"
    L"You can use it now; you will need to scan again after closing.",  // S_MB_SAVE_FAILED

    // 主窗口控件
    L"Sign Out",                     // S_MAIN_LOGOUT
    L"Room ID",                      // S_MAIN_ROOM_LABEL
    L"Fetching…",                    // S_MAIN_ROOM_FETCHING
    L"Fetch failed",                 // S_MAIN_ROOM_FAILED
    L"Live Category",                // S_MAIN_PART_LABEL
    L"Start Live",                   // S_MAIN_START
    L"Stop Live",                    // S_MAIN_STOP
    L"Retry",                        // S_MAIN_RETRY
    L"Clear Credentials & Re-login", // S_MAIN_RELOGIN
    L"Stream Address",               // S_MAIN_LABEL_ADDR
    L"Stream Key",                   // S_MAIN_LABEL_CODE
    L"Copy",                         // S_MAIN_COPY
    L"Copied",                       // S_MAIN_COPIED
    L"Set the items above in your streaming software (e.g. OBS):\r\n\"Server\": Stream "
    L"Address, \"Stream Key\": Stream Key.",  // S_MAIN_HINT
    L"Face verification required: scan the QR code below with the bilibili App.",  // S_MAIN_FACE_TITLE
    L"Cancel",  // S_MAIN_FACE_CANCEL
    L"Credentials are DPAPI-encrypted and kept in this folder only;\r\nthey are erased "
    L"immediately on sign-out or by deleting the folder.",  // S_MAIN_FOOTER

    // 主窗口状态栏
    L"Verifying local sign-in state and fetching room ID…",  // S_MS_VERIFYING
    L"Room ID not available yet; cannot start live. Click \"Retry\" or \"Re-login\".",  // S_MS_NO_ROOM_START
    L"Local credentials lack bili_jct; please sign in again.",  // S_MS_NO_CSRF
    L"Requesting to start live…",                               // S_MS_STARTING
    L"Room ID not available yet; cannot stop live.",            // S_MS_NO_ROOM_STOP
    L"Stopping live…",                                          // S_MS_STOPPING
    L"Signing out…",                                            // S_MS_LOGGING_OUT
    L"Verification failed (%1). Check network, then \"Retry\" or re-login.",  // S_MS_VERIFY_NET_FAIL
    L"Signed in. Welcome back! Choose a category to start streaming.",  // S_MS_LOGIN_WELCOME
    L"Entered the console with local credentials (no scan needed). Choose a category to start "
    L"streaming.",  // S_MS_AUTO_ENTER
    L"Failed to get room ID (this account may not have a live room).",  // S_MS_ROOM_FAIL
    L"Failed to get room ID (this account may not have a live room): %1.",  // S_MS_ROOM_FAIL_DETAIL
    L"Failed to get room ID (network error). Click \"Retry\" to try again.",  // S_MS_ROOM_NET_FAIL
    L"Failed to get room ID (network error): %1. Click \"Retry\" to try again.",  // S_MS_ROOM_NET_FAIL_DETAIL
    L"Failed to start live (network error): %1.",  // S_MS_START_NET_FAIL
    L"Face verification required: scan the QR code with the bilibili App, then click \"Start "
    L"Live\" again.",  // S_MS_FACE_NEEDED
    L"Face verification required, but the verification QR code could not be generated.",  // S_MS_FACE_QR_FAIL
    L"Face verification required, but the QR code could not be generated: %1.",  // S_MS_FACE_QR_FAIL_DETAIL
    L"Live started. Put the streaming info below into your streaming software (e.g. OBS).",  // S_MS_LIVE_STARTED
    L"Failed to start live: %1.",     // S_MS_START_FAIL
    L"Server returned no streaming info",  // S_MS_NO_RTMP
    L" (code=%1)",                    // S_MS_CODE_SUFFIX
    L"Failed to stop live (network error): %1.",  // S_MS_STOP_NET_FAIL
    L"Live stopped.",                 // S_MS_STOPPED
    L"Failed to stop live: %1 (code=%2).",  // S_MS_STOP_FAIL
    L"Sign-out was not completed.",   // S_MS_LOGOUT_INCOMPLETE
    L"network request failed",        // S_MS_NET_FAIL
    L"unknown error",                 // S_MS_UNKNOWN_ERR

    // 主窗口弹窗
    L"Sign out now?\n\nThe locally stored encrypted credentials will be immediately and "
    L"securely erased; you will need to scan the QR code again next time.",  // S_MB_LOGOUT_CONFIRM
    L"Re-login",  // S_MB_RELOGIN_TITLE
    L"Erase local credentials and return to the sign-in page?\n\nThe credential file will be "
    L"overwritten and erased; you will need to scan the QR code again.",  // S_MB_RELOGIN_CONFIRM
    L"Unable to reach the server to sign out (%1).\n\nErase local credentials only?\nNote: the "
    L"server-side session may still be valid; sign out again when the network is back.",  // S_MB_LOGOUT_NET_FAIL
};

// ---------------- 中文表 ----------------
const wchar_t* kZh[] = {
    L"",  // S_NONE

    // 通用 / 入口
    L"bilibili 扫码登录",  // S_APP_TITLE_LOGIN
    L"BiliLive 控制台",    // S_APP_TITLE_MAIN
    L"提示",               // S_MB_INFO_TITLE
    L"二维码加载中…",       // S_QR_LOADING
    L"二维码编码失败（内容过长）",  // S_QR_TOO_LONG
    L"检测到本地登录凭证无效、已损坏或来自其它用户，已安全清除。\n请重新扫码登录。",  // S_MB_BROKEN_SESSION

    // 凭证销毁提示
    L"未检测到有效的本地登录凭证，请重新扫码登录。",  // S_NOTICE_NO_SESSION
    L"本地登录凭证已失效或与记录不符，已安全清除。\n请重新扫码登录。",  // S_NOTICE_CRED_INVALID
    L"已退出登录，本地登录凭证已安全销毁。",  // S_NOTICE_LOGGED_OUT
    L"本地登录凭证已安全销毁。",              // S_NOTICE_LOCAL_WIPED
    L"服务端确认当前登录已失效，本地登录凭证已安全销毁。",  // S_NOTICE_SERVER_INVALID

    // 登录窗口
    L"刷新二维码",  // S_LOGIN_REFRESH
    L"登录凭证使用系统 DPAPI 加密，仅保存在本程序目录；\r\n点击「退出登录」后立即销毁。",  // S_LOGIN_HINT
    L"正在生成二维码…",  // S_LS_GENERATING
    L"请使用哔哩哔哩 App 扫描二维码登录",  // S_LS_SCAN_PROMPT
    L"二维码生成失败：%1。请点击「刷新二维码」重试",  // S_LS_GEN_FAILED
    L"网络异常，自动重试中…",             // S_LS_NET_RETRY
    L"登录成功但凭证不完整，正在刷新二维码…",  // S_LS_CRED_INCOMPLETE
    L"二维码已过期，正在刷新…",            // S_LS_EXPIRED
    L"已扫码，请在手机上确认登录",          // S_LS_SCANNED
    L"等待扫码结果（code=%1）…",           // S_LS_WAIT_CODE
    L"登录成功，但本地凭证保存失败（程序目录可能无写入权限）。\n本次可正常使用，关闭程序后需要重新扫码登录。",  // S_MB_SAVE_FAILED

    // 主窗口控件
    L"退出登录",                   // S_MAIN_LOGOUT
    L"房间号",                     // S_MAIN_ROOM_LABEL
    L"正在获取…",                  // S_MAIN_ROOM_FETCHING
    L"获取失败",                   // S_MAIN_ROOM_FAILED
    L"选择直播分区",               // S_MAIN_PART_LABEL
    L"开始直播",                   // S_MAIN_START
    L"停止直播",                   // S_MAIN_STOP
    L"重试",                       // S_MAIN_RETRY
    L"清除本地凭证并重新登录",     // S_MAIN_RELOGIN
    L"推流地址",                   // S_MAIN_LABEL_ADDR
    L"推流码",                     // S_MAIN_LABEL_CODE
    L"复制",                       // S_MAIN_COPY
    L"已复制",                     // S_MAIN_COPIED
    L"请将以上推流信息配置到您的直播软件（如 OBS）中：\r\n服务器填「推流地址」，串流密钥填「推流码」。",  // S_MAIN_HINT
    L"需要人脸认证：请使用哔哩哔哩 App 扫描下方二维码完成认证。",  // S_MAIN_FACE_TITLE
    L"取消",  // S_MAIN_FACE_CANCEL
    L"登录凭证经系统 DPAPI 加密，仅保存于本程序目录；点击「退出登录」或删除程序目录后即彻底失效。",  // S_MAIN_FOOTER

    // 主窗口状态栏
    L"正在验证本地登录状态并获取直播间号…",  // S_MS_VERIFYING
    L"尚未获取到直播间号，无法开始直播。请点击「重试」或「重新登录」。",  // S_MS_NO_ROOM_START
    L"本地凭证缺少 bili_jct，请重新登录。",  // S_MS_NO_CSRF
    L"正在请求开启直播…",                    // S_MS_STARTING
    L"尚未获取到直播间号，无法停止直播。",    // S_MS_NO_ROOM_STOP
    L"正在停止直播…",                        // S_MS_STOPPING
    L"正在退出登录…",                        // S_MS_LOGGING_OUT
    L"无法验证本地登录状态（%1）。请检查网络后点击「重试」或重新登录。",  // S_MS_VERIFY_NET_FAIL
    L"登录成功，欢迎回来！选择分区后即可开始直播。",  // S_MS_LOGIN_WELCOME
    L"已使用本地登录凭证自动进入控制台（免扫码）。选择分区后即可开始直播。",  // S_MS_AUTO_ENTER
    L"未能获取直播间号（账号可能未开通直播间）。",       // S_MS_ROOM_FAIL
    L"未能获取直播间号（账号可能未开通直播间）：%1。",  // S_MS_ROOM_FAIL_DETAIL
    L"直播间号获取失败（网络异常）。点击「重试」重新获取。",       // S_MS_ROOM_NET_FAIL
    L"直播间号获取失败（网络异常）：%1。点击「重试」重新获取。",  // S_MS_ROOM_NET_FAIL_DETAIL
    L"开启直播失败（网络异常）：%1。",  // S_MS_START_NET_FAIL
    L"需要人脸认证：请使用哔哩哔哩 App 扫码完成认证，然后重新点击「开始直播」。",  // S_MS_FACE_NEEDED
    L"需要人脸认证，但认证二维码生成失败。",        // S_MS_FACE_QR_FAIL
    L"需要人脸认证，但认证二维码生成失败：%1。",    // S_MS_FACE_QR_FAIL_DETAIL
    L"直播已开启：请将下方推流信息填入直播软件（如 OBS）。",  // S_MS_LIVE_STARTED
    L"开启直播失败：%1。",          // S_MS_START_FAIL
    L"服务端未返回推流信息",        // S_MS_NO_RTMP
    L"（code=%1）",                 // S_MS_CODE_SUFFIX
    L"停止直播失败（网络异常）：%1。",  // S_MS_STOP_NET_FAIL
    L"已停止直播。",                // S_MS_STOPPED
    L"停止直播失败：%1（code=%2）。",  // S_MS_STOP_FAIL
    L"退出登录未完成。",            // S_MS_LOGOUT_INCOMPLETE
    L"网络请求失败",                // S_MS_NET_FAIL
    L"未知错误",                    // S_MS_UNKNOWN_ERR

    // 主窗口弹窗
    L"确定要退出登录吗？\n\n退出后将立即安全销毁本地保存的加密登录凭证，下次需要重新扫码登录。",  // S_MB_LOGOUT_CONFIRM
    L"重新登录",  // S_MB_RELOGIN_TITLE
    L"确定要清除本地登录凭证并返回登录页吗？\n\n凭证文件将被覆写销毁，需要重新扫码登录。",  // S_MB_RELOGIN_CONFIRM
    L"无法连接服务器完成退出登录（%1）。\n\n是否仅清除本地登录凭证？\n注意：服务端登录状态可能仍然有效，建议联网后重新「退出登录」。",  // S_MB_LOGOUT_NET_FAIL
};

static_assert(sizeof(kEn) / sizeof(kEn[0]) == (size_t)StrId::S_COUNT,
              "kEn 表项数量与 StrId 不一致");
static_assert(sizeof(kZh) / sizeof(kZh[0]) == (size_t)StrId::S_COUNT,
              "kZh 表项数量与 StrId 不一致");

std::wstring SettingsPath() {
    return util::JoinPath(util::ExeDir(), kSettingsFileName);
}

}  // namespace

const wchar_t* kSettingsFileName = L"bili_settings.ini";

Lang GetLang() { return g_lang; }

void SetLang(Lang lang) { g_lang = lang; }

bool LoadLangSetting() {
    HANDLE f = CreateFileW(SettingsPath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;

    char buf[256] = {};
    DWORD rd = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &rd, nullptr);
    CloseHandle(f);

    std::string text(buf, rd);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = util::Trim(
            text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        std::string low = util::ToLowerAscii(line);
        if (util::StartsWith(low, "lang=")) {
            std::string v = util::Trim(low.substr(5));
            if (v == "zh") {
                g_lang = Lang::Zh;
                return true;
            }
            if (v == "en") {
                g_lang = Lang::En;
                return true;
            }
        }
    }
    return false;
}

bool SaveLangSetting() {
    std::string content = std::string("lang=") + (g_lang == Lang::Zh ? "zh" : "en") + "\r\n";
    HANDLE f = CreateFileW(SettingsPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = WriteFile(f, content.data(), (DWORD)content.size(), &wr, nullptr) &&
              wr == content.size();
    CloseHandle(f);
    return ok;
}

const wchar_t* T(StrId id) {
    int i = (int)id;
    if (i <= 0 || i >= (int)StrId::S_COUNT) return L"";
    return g_lang == Lang::Zh ? kZh[i] : kEn[i];
}

std::wstring F(StrId id, const std::wstring& a1, const std::wstring& a2) {
    std::wstring t = T(id);
    if (a1.empty() && a2.empty()) return t;
    std::wstring out;
    out.reserve(t.size() + a1.size() + a2.size());
    for (size_t i = 0; i < t.size();) {
        if (t[i] == L'%' && i + 1 < t.size() && (t[i + 1] == L'1' || t[i + 1] == L'2')) {
            out += (t[i + 1] == L'1') ? a1 : a2;
            i += 2;
        } else {
            out.push_back(t[i]);
            ++i;
        }
    }
    return out;
}

const wchar_t* LangSwitchLabel() { return g_lang == Lang::Zh ? L"English" : L"中文"; }

bool CheckTablesComplete() {
    for (int i = 1; i < (int)StrId::S_COUNT; ++i) {
        if (!kEn[i] || !*kEn[i] || !kZh[i] || !*kZh[i]) return false;
    }
    return true;
}

}  // namespace i18n
