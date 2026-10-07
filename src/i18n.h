// i18n.h - 界面多语言（默认英文 / 支持中文），字符串表 + 轻量模板渲染
//
// 设计：
//  * 全部 UI 文案集中在 i18n.cpp 的两张表（en / zh），按 StrId 一一对应；
//  * 语言偏好持久化到 <exe 目录>\bili_settings.ini（非敏感信息，不含任何凭证）；
//  * 模板占位符 %1 / %2 由 F() 渲染；状态栏保存「模板键 + 参数」，切换语言后
//    按新语言重新渲染。
#pragma once

#include <string>

namespace i18n {

enum class Lang { En, Zh };

// 全量文案键（kEn / kZh 两表按同一顺序定义）
enum StrId {
    S_NONE = 0,  // 无文本（空状态占位）

    // ---- 通用 / 入口 ----
    S_APP_TITLE_LOGIN,     // bilibili 扫码登录
    S_APP_TITLE_MAIN,      // BiliLive 控制台
    S_MB_INFO_TITLE,       // 提示
    S_QR_LOADING,          // 二维码加载中…
    S_QR_TOO_LONG,         // 二维码编码失败（内容过长）
    S_MB_BROKEN_SESSION,   // 启动时检测到凭证损坏并已销毁

    // ---- 凭证销毁提示（WipeSessionAndToLogin） ----
    S_NOTICE_NO_SESSION,     // 未检测到有效凭证
    S_NOTICE_CRED_INVALID,   // 凭证失效或与记录不符
    S_NOTICE_LOGGED_OUT,     // 已退出登录
    S_NOTICE_LOCAL_WIPED,    // 仅清除本地凭证
    S_NOTICE_SERVER_INVALID, // 服务端确认登录已失效

    // ---- 登录窗口 ----
    S_LOGIN_REFRESH,         // 刷新二维码
    S_LOGIN_HINT,            // 底部安全说明
    S_LS_GENERATING,         // 正在生成二维码…
    S_LS_SCAN_PROMPT,        // 请扫码登录
    S_LS_GEN_FAILED,         // %1 = 失败原因
    S_LS_NET_RETRY,          // 网络异常自动重试
    S_LS_CRED_INCOMPLETE,    // 凭证不完整刷新中
    S_LS_EXPIRED,            // 二维码已过期
    S_LS_SCANNED,            // 已扫码待确认
    S_LS_WAIT_CODE,          // %1 = code
    S_MB_SAVE_FAILED,        // 凭证保存失败提示

    // ---- 主窗口控件 ----
    S_MAIN_LOGOUT,           // 退出登录（按钮 / 登出弹窗标题）
    S_MAIN_ROOM_LABEL,       // 房间号
    S_MAIN_ROOM_FETCHING,    // 正在获取…
    S_MAIN_ROOM_FAILED,      // 获取失败
    S_MAIN_PART_LABEL,       // 选择直播分区
    S_MAIN_START,            // 开始直播
    S_MAIN_STOP,             // 停止直播
    S_MAIN_RETRY,            // 重试
    S_MAIN_RELOGIN,          // 清除本地凭证并重新登录
    S_MAIN_LABEL_ADDR,       // 推流地址
    S_MAIN_LABEL_CODE,       // 推流码
    S_MAIN_COPY,             // 复制
    S_MAIN_COPIED,           // 已复制
    S_MAIN_HINT,             // 推流配置说明
    S_MAIN_FACE_TITLE,       // 人脸认证标题
    S_MAIN_FACE_CANCEL,      // 取消
    S_MAIN_FOOTER,           // 底部安全说明

    // ---- 主窗口状态栏 ----
    S_MS_VERIFYING,            // 正在验证并获取直播间号
    S_MS_NO_ROOM_START,        // 无房间号不可开播
    S_MS_NO_CSRF,              // 缺少 bili_jct
    S_MS_STARTING,             // 正在请求开启直播
    S_MS_NO_ROOM_STOP,         // 无房间号不可停播
    S_MS_STOPPING,             // 正在停止直播
    S_MS_LOGGING_OUT,          // 正在退出登录
    S_MS_VERIFY_NET_FAIL,      // %1 = 网络错误
    S_MS_LOGIN_WELCOME,        // 登录成功欢迎
    S_MS_AUTO_ENTER,           // 免扫码自动进入
    S_MS_ROOM_FAIL,            // 房间号获取失败
    S_MS_ROOM_FAIL_DETAIL,     // %1 = 服务端消息
    S_MS_ROOM_NET_FAIL,        // 房间号网络失败
    S_MS_ROOM_NET_FAIL_DETAIL, // %1 = 网络错误
    S_MS_START_NET_FAIL,       // %1 = 网络错误
    S_MS_FACE_NEEDED,          // 需要人脸认证
    S_MS_FACE_QR_FAIL,         // 人脸二维码生成失败
    S_MS_FACE_QR_FAIL_DETAIL,  // %1 = 原因
    S_MS_LIVE_STARTED,         // 直播已开启
    S_MS_START_FAIL,           // %1 = 消息
    S_MS_NO_RTMP,              // 服务端未返回推流信息
    S_MS_CODE_SUFFIX,          // %1 = code
    S_MS_STOP_NET_FAIL,        // %1 = 网络错误
    S_MS_STOPPED,              // 已停止直播
    S_MS_STOP_FAIL,            // %1 = 消息, %2 = code
    S_MS_LOGOUT_INCOMPLETE,    // 退出登录未完成
    S_MS_NET_FAIL,             // network request failed
    S_MS_UNKNOWN_ERR,          // unknown error

    // ---- 主窗口弹窗 ----
    S_MB_LOGOUT_CONFIRM,       // 退出登录确认
    S_MB_RELOGIN_TITLE,        // 重新登录（弹窗标题）
    S_MB_RELOGIN_CONFIRM,      // 重新登录确认
    S_MB_LOGOUT_NET_FAIL,      // %1 = 网络错误

    S_COUNT
};

Lang GetLang();
void SetLang(Lang lang);

// 从 <exe 目录>\bili_settings.ini 读取语言（缺失/无效时保持默认英文）。
// 返回是否读到有效值。
bool LoadLangSetting();
// 保存当前语言到设置文件。失败返回 false。
bool SaveLangSetting();

// 当前语言下 id 对应的字符串（S_NONE / 越界返回空串）
const wchar_t* T(StrId id);

// 渲染模板：替换占位符 %1 / %2
std::wstring F(StrId id, const std::wstring& a1 = L"", const std::wstring& a2 = L"");

// 语言切换按钮文本（显示目标语言的本名："中文" / "English"）
const wchar_t* LangSwitchLabel();

// en / zh 两表是否全部完整非空（自检用）
bool CheckTablesComplete();

// 设置文件名（与主程序 exe 同目录）
extern const wchar_t* kSettingsFileName;

}  // namespace i18n
