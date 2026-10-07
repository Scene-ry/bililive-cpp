// app.h - 应用共享状态 / 自定义消息 / 跨文件声明
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "bili_api.h"
#include "i18n.h"
#include "session.h"
#include "util.h"

// ---- 自定义消息（载荷均为堆对象，主线程处理完 delete） ----
#define WM_APP_LOGIN_QR (WM_APP + 1)  // LoginQrPayload*
#define WM_APP_POLL (WM_APP + 2)      // PollPayload*
#define WM_APP_START (WM_APP + 3)     // StartPayload*
#define WM_APP_STOP (WM_APP + 4)      // SimpleApiPayload*
#define WM_APP_LOGOUT (WM_APP + 5)    // SimpleApiPayload*
#define WM_APP_VERIFY (WM_APP + 6)    // VerifyPayload*

// ---- 异步任务载荷 ----
struct LoginQrPayload {
    bool ok = false;    // generate 接口成功
    std::string error;  // generate 错误
    std::string qrcodeKey;
    std::string url;
    bool qrOk = false;  // 本地编码成功
    i18n::StrId qrErrorId = i18n::S_NONE;  // 本地编码失败原因（模板键）
    std::vector<std::vector<bool>> modules;
    int version = 0;
};

struct PollPayload {
    bili::QrPollResult result;
};

struct StartPayload {
    bili::StartLiveResult result;
    bool faceQrOk = false;
    i18n::StrId faceQrErrorId = i18n::S_NONE;  // 人脸二维码编码失败原因（模板键）
    std::vector<std::vector<bool>> faceModules;
};

struct SimpleApiPayload {
    bili::SimpleApiResult result;
};

struct VerifyPayload {
    bool navTransportOk = false;
    bool navIsLogin = false;
    int64_t navMid = 0;
    bool roomTransportOk = false;
    int roomCode = -1;
    int64_t roomId = 0;
    std::string error;  // 第一个网络错误描述
};

// ---- 状态栏视图：保存「模板键 + 参数」，切换语言后可按新语言重渲染 ----
struct StatusView {
    i18n::StrId key = i18n::S_NONE;
    std::wstring a1, a2;
    bool err = false;
};

// ---- 全局应用状态 ----
struct AppState {
    HINSTANCE hInst = nullptr;

    // 会话
    SessionData session;
    bool sessionValid = false;
    bool justLoggedIn = false;  // 本次启动内刚扫码登录（用于状态文案）

    // 登录窗口
    HWND loginWnd = nullptr;
    HWND hLoginQr = nullptr;
    HWND hLoginStatus = nullptr;
    HWND hLoginRefresh = nullptr;
    HWND hLoginLang = nullptr;
    std::string qrcodeKey;
    bool generateInFlight = false;
    bool pollInFlight = false;
    StatusView loginStatus;

    // 主窗口
    HWND mainWnd = nullptr;
    HWND hRoom = nullptr;
    HWND hPartLabel = nullptr;
    HWND hCombo = nullptr;
    HWND hStart = nullptr;
    HWND hStop = nullptr;
    HWND hLogout = nullptr;
    HWND hMainLang = nullptr;
    HWND hStatus = nullptr;
    HWND hRetry = nullptr;
    HWND hRelogin = nullptr;
    HWND hFooter = nullptr;
    // 人脸认证块
    HWND hFaceTitle = nullptr;
    HWND hFaceQr = nullptr;
    HWND hFaceCancel = nullptr;
    // 推流信息块
    HWND hLabelAddr = nullptr;
    HWND hAddrEdit = nullptr;
    HWND hCopyAddr = nullptr;
    HWND hLabelCode = nullptr;
    HWND hCodeEdit = nullptr;
    HWND hCopyCode = nullptr;
    HWND hHint = nullptr;

    std::vector<std::string> partitionIds;
    std::string selectedPartitionId;
    std::string roomId;
    bool roomKnown = false;

    bool verifyInFlight = false;
    bool startInFlight = false;
    bool stopInFlight = false;
    bool logoutInFlight = false;
    StatusView mainStatus;
};

extern AppState g;

// ---- UI 缩放 / 字体 ----
int S(int v);  // 按 DPI 缩放
extern HFONT g_fontUI;
extern HFONT g_fontTitle;
extern HFONT g_fontSmall;
void InitUI(HINSTANCE hInst);
void DestroyUIFonts();

// ---- 通用辅助 ----
inline std::wstring W(const std::string& s) { return util::Utf8ToWide(s); }

template <typename T>
void PostPayload(HWND hwnd, UINT msg, T* payload) {
    if (!hwnd || !IsWindow(hwnd) || !PostMessageW(hwnd, msg, 0, (LPARAM)payload)) {
        delete payload;
    }
}

// ---- 窗口类注册 / 创建 ----
extern const wchar_t* kQrPanelClass;
extern const wchar_t* kLoginWndClass;
extern const wchar_t* kMainWndClass;
void RegisterAppClasses(HINSTANCE hInst);
void CenterWindowOnScreen(HWND hwnd);
void ResizeClientToSize(HWND hwnd, int cx, int cy);
HWND CreateLoginWindow();
HWND CreateMainWindow();
void SwitchToMainWindow();   // 主窗建、登录窗毁
void SwitchToLoginWindow();  // 登录窗建、主窗毁
void WipeSessionAndToLogin(i18n::StrId notice);  // 销毁凭证并回登录页（S_NONE = 无提示）

// ---- 二维码面板控件 ----
void QrPanelSetContent(HWND panel, const std::vector<std::vector<bool>>& modules);
void QrPanelClear(HWND panel);

// ---- 状态栏文本（保存模板键，切换语言后重渲染） ----
void SetLoginStatus(i18n::StrId key, bool isError, const std::wstring& a1 = L"",
                    const std::wstring& a2 = L"");
void SetMainStatus(i18n::StrId key, bool isError, const std::wstring& a1 = L"",
                   const std::wstring& a2 = L"");
void ApplyMainEnabledState();
void ShowMainVerifyButtons(bool showRetry, bool showRelogin);
void ShowMainRtmpBlock(bool show);
void ShowMainFaceBlock(bool show);

// ---- 多语言 ----
void SwitchLanguageAndRefresh();       // 切换语言 + 持久化 + 刷新全部窗口
void RefreshLoginWindowText(HWND hwnd);  // 按当前语言刷新登录窗口文本
void RefreshMainWindowText(HWND hwnd);   // 按当前语言刷新主窗口文本

// ---- 登录窗口内部 ----
void LoginStartGenerate(HWND hwnd);

// ---- 主窗口内部 ----
void MainStartVerify(HWND hwnd);
void MainStartLiveRequest(HWND hwnd);
void MainStopLiveRequest(HWND hwnd);
void MainLogoutRequest(HWND hwnd);
