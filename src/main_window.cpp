// main_window.cpp - 直播控制台主窗口（对应原 /tools/bililive 主页面）
//
// 自动登录流程（免扫码）：
//  启动 -> 加载 DPAPI 会话 -> 请求 /x/web-interface/nav 校验 cookie 登录态
//    * 失效 / 用户不一致 -> 覆写销毁本地凭证 -> 回登录页
//    * 有效             -> 请求 /room/v2/Room/room_id_by_uid 获取直播间号 -> 直接进入控制台
#include "app.h"
#include "partitions_data.h"
#include "qrcode.h"

#include <thread>

namespace {

const int IDC_MAIN_TITLE = 3001;
const int IDC_MAIN_LOGOUT = 3002;
const int IDC_MAIN_ROOM_LABEL = 3003;
const int IDC_MAIN_ROOM = 3004;
const int IDC_MAIN_PART_LABEL = 3005;
const int IDC_MAIN_COMBO = 3006;
const int IDC_MAIN_START = 3007;
const int IDC_MAIN_STOP = 3008;
const int IDC_MAIN_STATUS = 3009;
const int IDC_MAIN_RETRY = 3010;
const int IDC_MAIN_RELOGIN = 3011;
const int IDC_MAIN_LABEL_ADDR = 3012;
const int IDC_MAIN_ADDR = 3013;
const int IDC_MAIN_COPY_ADDR = 3014;
const int IDC_MAIN_LABEL_CODE = 3015;
const int IDC_MAIN_CODE = 3016;
const int IDC_MAIN_COPY_CODE = 3017;
const int IDC_MAIN_HINT = 3018;
const int IDC_MAIN_FACE_TITLE = 3019;
const int IDC_MAIN_FACE_QR = 3020;
const int IDC_MAIN_FACE_CANCEL = 3021;
const int IDC_MAIN_FOOTER = 3022;
const int IDC_MAIN_LANG = 3023;

const UINT_PTR TIMER_COPY_ADDR = 101;
const UINT_PTR TIMER_COPY_CODE = 102;
const UINT kCopyResetMs = 2000;

HWND MakeCtl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD exStyle, int x,
             int y, int w, int h, int id) {
    return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                           parent, (HMENU)(INT_PTR)id, g.hInst, nullptr);
}

void SetFontTo(HWND hwnd, int id, HFONT font) {
    HWND ctl = GetDlgItem(hwnd, id);
    if (ctl) SendMessageW(ctl, WM_SETFONT, (WPARAM)font, TRUE);
}

void ApplyMainFonts(HWND hwnd) {
    SetFontTo(hwnd, IDC_MAIN_TITLE, g_fontTitle);
    const int ids[] = {IDC_MAIN_LOGOUT,     IDC_MAIN_ROOM_LABEL, IDC_MAIN_ROOM,
                       IDC_MAIN_PART_LABEL, IDC_MAIN_COMBO,      IDC_MAIN_START,
                       IDC_MAIN_STOP,       IDC_MAIN_STATUS,     IDC_MAIN_RETRY,
                       IDC_MAIN_RELOGIN,    IDC_MAIN_LABEL_ADDR, IDC_MAIN_ADDR,
                       IDC_MAIN_COPY_ADDR,  IDC_MAIN_LABEL_CODE, IDC_MAIN_CODE,
                       IDC_MAIN_COPY_CODE,  IDC_MAIN_FACE_TITLE, IDC_MAIN_FACE_CANCEL,
                       IDC_MAIN_LANG};
    for (int id : ids) SetFontTo(hwnd, id, g_fontUI);
    SetFontTo(hwnd, IDC_MAIN_HINT, g_fontSmall);
    SetFontTo(hwnd, IDC_MAIN_FOOTER, g_fontSmall);
}

std::wstring GetCtlText(HWND hwnd, int id) {
    wchar_t buf[1024] = {};
    GetDlgItemTextW(hwnd, id, buf, 1024);
    return buf;
}

int FindPartitionIndex(const std::string& id) {
    for (int i = 0; i < kPartitionCount; ++i) {
        if (id == kPartitions[i].id) return i;
    }
    return -1;
}

void FillPartitionCombo() {
    g.partitionIds.clear();
    for (int i = 0; i < kPartitionCount; ++i) {
        g.partitionIds.push_back(kPartitions[i].id);
        SendMessageW(g.hCombo, CB_ADDSTRING, 0, (LPARAM)W(kPartitions[i].name).c_str());
    }
    SendMessageW(g.hCombo, CB_SETITEMHEIGHT, (WPARAM)-1, S(24));
    SendMessageW(g.hCombo, CB_SETITEMHEIGHT, 0, S(22));
    int idx = FindPartitionIndex(kDefaultPartitionId);
    if (idx < 0) idx = 0;
    SendMessageW(g.hCombo, CB_SETCURSEL, (WPARAM)idx, 0);
    if (idx < (int)g.partitionIds.size()) g.selectedPartitionId = g.partitionIds[(size_t)idx];
}

void CreateMainControls(HWND hwnd) {
    g.hRoom = nullptr;  // 由下面统一赋值
    MakeCtl(hwnd, L"STATIC", i18n::T(i18n::S_APP_TITLE_MAIN), SS_LEFT, 0, 20, 18, 260, 32,
            IDC_MAIN_TITLE);
    g.hLogout = MakeCtl(hwnd, L"BUTTON", i18n::T(i18n::S_MAIN_LOGOUT), BS_PUSHBUTTON | WS_TABSTOP,
                        0, 430, 16, 110, 32, IDC_MAIN_LOGOUT);
    g.hMainLang = MakeCtl(hwnd, L"BUTTON", i18n::LangSwitchLabel(), BS_PUSHBUTTON, 0, 340, 16, 80,
                          32, IDC_MAIN_LANG);

    g.hPartLabel = MakeCtl(hwnd, L"STATIC", i18n::T(i18n::S_MAIN_ROOM_LABEL), SS_LEFT, 0, 20, 66, 76,
                           24, IDC_MAIN_ROOM_LABEL);
    g.hRoom = MakeCtl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP, WS_EX_CLIENTEDGE,
                      104, 62, 436, 28, IDC_MAIN_ROOM);

    MakeCtl(hwnd, L"STATIC", i18n::T(i18n::S_MAIN_PART_LABEL), SS_LEFT, 0, 20, 104, 120, 24,
            IDC_MAIN_PART_LABEL);
    g.hCombo = MakeCtl(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 104,
                       100, 436, 300, IDC_MAIN_COMBO);

    g.hStart = MakeCtl(hwnd, L"BUTTON", i18n::T(i18n::S_MAIN_START), BS_PUSHBUTTON | WS_TABSTOP, 0,
                       20, 142, 254, 42, IDC_MAIN_START);
    g.hStop = MakeCtl(hwnd, L"BUTTON", i18n::T(i18n::S_MAIN_STOP), BS_PUSHBUTTON | WS_TABSTOP, 0, 286,
                      142, 254, 42, IDC_MAIN_STOP);

    g.hStatus = MakeCtl(hwnd, L"STATIC", L"", SS_LEFT, 0, 20, 194, 520, 40, IDC_MAIN_STATUS);
    g.hRetry = MakeCtl(hwnd, L"BUTTON", i18n::T(i18n::S_MAIN_RETRY), BS_PUSHBUTTON | WS_TABSTOP, 0,
                       20, 240, 254, 34, IDC_MAIN_RETRY);
    g.hRelogin = MakeCtl(hwnd, L"BUTTON", i18n::T(i18n::S_MAIN_RELOGIN),
                         BS_PUSHBUTTON | WS_TABSTOP, 0, 286, 240, 254, 34, IDC_MAIN_RELOGIN);

    // 推流信息块（默认隐藏）
    g.hLabelAddr = MakeCtl(hwnd, L"STATIC", i18n::T(i18n::S_MAIN_LABEL_ADDR), SS_LEFT, 0, 20, 286, 80,
                           24, IDC_MAIN_LABEL_ADDR);
    g.hAddrEdit = MakeCtl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, WS_EX_CLIENTEDGE, 104,
                          283, 372, 26, IDC_MAIN_ADDR);
    g.hCopyAddr = MakeCtl(hwnd, L"BUTTON", i18n::T(i18n::S_MAIN_COPY), BS_PUSHBUTTON, 0, 484, 282,
                          56, 28, IDC_MAIN_COPY_ADDR);
    g.hLabelCode = MakeCtl(hwnd, L"STATIC", i18n::T(i18n::S_MAIN_LABEL_CODE), SS_LEFT, 0, 20, 322, 80,
                           24, IDC_MAIN_LABEL_CODE);
    g.hCodeEdit = MakeCtl(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, WS_EX_CLIENTEDGE, 104,
                          319, 372, 26, IDC_MAIN_CODE);
    g.hCopyCode = MakeCtl(hwnd, L"BUTTON", i18n::T(i18n::S_MAIN_COPY), BS_PUSHBUTTON, 0, 484, 318,
                          56, 28, IDC_MAIN_COPY_CODE);
    g.hHint = MakeCtl(hwnd, L"STATIC", i18n::T(i18n::S_MAIN_HINT), SS_LEFT, 0, 20, 352, 520, 44,
                      IDC_MAIN_HINT);

    // 人脸认证块（默认隐藏）
    g.hFaceTitle = MakeCtl(hwnd, L"STATIC", i18n::T(i18n::S_MAIN_FACE_TITLE), SS_CENTER, 0, 20, 286,
                           520, 24, IDC_MAIN_FACE_TITLE);
    g.hFaceQr = CreateWindowExW(0, kQrPanelClass, L"", WS_CHILD, S(170), S(320), S(220), S(220), hwnd,
                                (HMENU)(INT_PTR)IDC_MAIN_FACE_QR, g.hInst, nullptr);
    g.hFaceCancel =
        MakeCtl(hwnd, L"BUTTON", i18n::T(i18n::S_MAIN_FACE_CANCEL), BS_PUSHBUTTON, 0, 230, 550, 100,
                30, IDC_MAIN_FACE_CANCEL);

    g.hFooter = MakeCtl(hwnd, L"STATIC", i18n::T(i18n::S_MAIN_FOOTER), SS_CENTER, 0, 20, 596, 520,
                        40, IDC_MAIN_FOOTER);

    ApplyMainFonts(hwnd);
    FillPartitionCombo();
}

}  // namespace

// ---------------- 自动登录验证（免扫码入口） ----------------
void MainStartVerify(HWND hwnd) {
    if (g.verifyInFlight) return;
    if (!g.sessionValid) {
        WipeSessionAndToLogin(i18n::S_NOTICE_NO_SESSION);
        return;
    }
    g.verifyInFlight = true;
    ShowMainVerifyButtons(false, false);
    SetMainStatus(i18n::S_MS_VERIFYING, false);
    ApplyMainEnabledState();

    std::string cookie = g.session.cookie;
    std::string uid = g.session.uid;
    std::thread([hwnd, cookie, uid]() {
        VerifyPayload* p = new VerifyPayload();
        bili::NavResult nav = bili::NavCheck(cookie);
        p->navTransportOk = nav.transportOk;
        p->navIsLogin = nav.isLogin;
        p->navMid = nav.mid;
        if (!nav.transportOk) {
            p->error = nav.error;  // 空则由主线程兜底
            PostPayload(hwnd, WM_APP_VERIFY, p);
            return;
        }
        // 明确未登录，或凭证对应的用户与记录不一致 -> 视为凭证失效
        if (!nav.isLogin) {
            p->navIsLogin = false;
            PostPayload(hwnd, WM_APP_VERIFY, p);
            return;
        }
        if (nav.mid != 0 && !uid.empty() && std::to_string(nav.mid) != uid) {
            p->navIsLogin = false;
            p->error = "凭证用户与本地记录不一致";
            PostPayload(hwnd, WM_APP_VERIFY, p);
            return;
        }
        // 登录态有效：获取直播间号
        bili::RoomInfoResult room = bili::RoomIdByUid(uid);
        p->roomTransportOk = room.transportOk;
        p->roomCode = room.code;
        p->roomId = room.roomId;
        if (room.transportOk && room.code != 0 && p->error.empty()) p->error = room.message;
        if (!room.transportOk && p->error.empty()) p->error = room.error;
        PostPayload(hwnd, WM_APP_VERIFY, p);
    }).detach();
}

// ---------------- 开始 / 停止直播 ----------------
void MainStartLiveRequest(HWND hwnd) {
    if (g.startInFlight || !g.sessionValid) return;
    if (!g.roomKnown || g.roomId.empty()) {
        SetMainStatus(i18n::S_MS_NO_ROOM_START, true);
        return;
    }
    std::string cookie = g.session.cookie;
    std::string csrf = util::CookieValue(cookie, "bili_jct");
    if (csrf.empty()) {
        SetMainStatus(i18n::S_MS_NO_CSRF, true);
        return;
    }
    std::string roomId = g.roomId;
    std::string partitionId = g.selectedPartitionId;

    g.startInFlight = true;
    ShowMainFaceBlock(false);
    SetMainStatus(i18n::S_MS_STARTING, false);
    ApplyMainEnabledState();

    std::thread([hwnd, cookie, csrf, roomId, partitionId]() {
        StartPayload* p = new StartPayload();
        p->result = bili::StartLive(cookie, csrf, roomId, partitionId);
        if (p->result.transportOk && p->result.needFaceAuth && !p->result.faceQrUrl.empty()) {
            int ver = 0;
            if (qrc::EncodeText(p->result.faceQrUrl, qrc::Ecc::M, p->faceModules, ver)) {
                p->faceQrOk = true;
            } else {
                p->faceQrErrorId = i18n::S_QR_TOO_LONG;
            }
        }
        PostPayload(hwnd, WM_APP_START, p);
    }).detach();
}

void MainStopLiveRequest(HWND hwnd) {
    if (g.stopInFlight || !g.sessionValid) return;
    if (!g.roomKnown || g.roomId.empty()) {
        SetMainStatus(i18n::S_MS_NO_ROOM_STOP, true);
        return;
    }
    std::string cookie = g.session.cookie;
    std::string csrf = util::CookieValue(cookie, "bili_jct");
    std::string roomId = g.roomId;

    g.stopInFlight = true;
    SetMainStatus(i18n::S_MS_STOPPING, false);
    ApplyMainEnabledState();

    std::thread([hwnd, cookie, csrf, roomId]() {
        SimpleApiPayload* p = new SimpleApiPayload();
        p->result = bili::StopLive(cookie, csrf, roomId);
        PostPayload(hwnd, WM_APP_STOP, p);
    }).detach();
}

// ---------------- 退出登录 ----------------
void MainLogoutRequest(HWND hwnd) {
    if (g.logoutInFlight || !g.sessionValid) return;
    int rc = MessageBoxW(hwnd, i18n::T(i18n::S_MB_LOGOUT_CONFIRM),
                         i18n::T(i18n::S_MAIN_LOGOUT), MB_ICONQUESTION | MB_YESNO);
    if (rc != IDYES) return;

    std::string cookie = g.session.cookie;
    std::string csrf = util::CookieValue(cookie, "bili_jct");

    g.logoutInFlight = true;
    SetMainStatus(i18n::S_MS_LOGGING_OUT, false);
    ApplyMainEnabledState();

    std::thread([hwnd, cookie, csrf]() {
        SimpleApiPayload* p = new SimpleApiPayload();
        p->result = bili::Logout(cookie, csrf);
        PostPayload(hwnd, WM_APP_LOGOUT, p);
    }).detach();
}

// ---------------- 窗口创建与消息循环 ----------------
HWND CreateMainWindow() {
    HWND hwnd = CreateWindowExW(0, kMainWndClass, i18n::T(i18n::S_APP_TITLE_MAIN),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0, 620,
                                720, nullptr, nullptr, g.hInst, nullptr);
    if (!hwnd) return nullptr;
    ResizeClientToSize(hwnd, S(560), S(640));
    CenterWindowOnScreen(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.mainWnd = hwnd;
            g.verifyInFlight = g.startInFlight = g.stopInFlight = g.logoutInFlight = false;
            g.mainStatus = StatusView();  // 重建窗口时重置状态

            CreateMainControls(hwnd);
            ShowMainVerifyButtons(false, false);
            ShowMainRtmpBlock(false);
            ShowMainFaceBlock(false);
            SetWindowTextW(g.hRoom, g.roomKnown ? W(g.roomId).c_str()
                                                : i18n::T(i18n::S_MAIN_ROOM_FETCHING));

            ApplyMainEnabledState();
            MainStartVerify(hwnd);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if ((HWND)lp == g.hCombo && code == CBN_SELCHANGE) {
                int idx = (int)SendMessageW(g.hCombo, CB_GETCURSEL, 0, 0);
                if (idx >= 0 && idx < (int)g.partitionIds.size()) {
                    g.selectedPartitionId = g.partitionIds[(size_t)idx];
                }
                return 0;
            }
            if (code != BN_CLICKED) return 0;
            if (id == IDC_MAIN_START) {
                MainStartLiveRequest(hwnd);
            } else if (id == IDC_MAIN_STOP) {
                MainStopLiveRequest(hwnd);
            } else if (id == IDC_MAIN_LOGOUT) {
                MainLogoutRequest(hwnd);
            } else if (id == IDC_MAIN_LANG) {
                SwitchLanguageAndRefresh();
            } else if (id == IDC_MAIN_RETRY) {
                MainStartVerify(hwnd);
            } else if (id == IDC_MAIN_RELOGIN) {
                int rc = MessageBoxW(hwnd, i18n::T(i18n::S_MB_RELOGIN_CONFIRM),
                                     i18n::T(i18n::S_MB_RELOGIN_TITLE), MB_ICONQUESTION | MB_YESNO);
                if (rc == IDYES) WipeSessionAndToLogin(i18n::S_NONE);
            } else if (id == IDC_MAIN_COPY_ADDR) {
                std::wstring text = GetCtlText(hwnd, IDC_MAIN_ADDR);
                if (!text.empty() && util::CopyTextToClipboard(hwnd, text)) {
                    SetWindowTextW(g.hCopyAddr, i18n::T(i18n::S_MAIN_COPIED));
                    SetTimer(hwnd, TIMER_COPY_ADDR, kCopyResetMs, nullptr);
                }
            } else if (id == IDC_MAIN_COPY_CODE) {
                std::wstring text = GetCtlText(hwnd, IDC_MAIN_CODE);
                if (!text.empty() && util::CopyTextToClipboard(hwnd, text)) {
                    SetWindowTextW(g.hCopyCode, i18n::T(i18n::S_MAIN_COPIED));
                    SetTimer(hwnd, TIMER_COPY_CODE, kCopyResetMs, nullptr);
                }
            } else if (id == IDC_MAIN_FACE_CANCEL) {
                ShowMainFaceBlock(false);
            }
            return 0;
        }

        case WM_TIMER: {
            if (wp == TIMER_COPY_ADDR) {
                KillTimer(hwnd, TIMER_COPY_ADDR);
                SetWindowTextW(g.hCopyAddr, i18n::T(i18n::S_MAIN_COPY));
            } else if (wp == TIMER_COPY_CODE) {
                KillTimer(hwnd, TIMER_COPY_CODE);
                SetWindowTextW(g.hCopyCode, i18n::T(i18n::S_MAIN_COPY));
            }
            return 0;
        }

        case WM_APP_VERIFY: {
            VerifyPayload* p = (VerifyPayload*)lp;
            g.verifyInFlight = false;

            if (!p->navTransportOk) {
                std::wstring err = W(p->error);
                if (err.empty()) err = i18n::T(i18n::S_MS_NET_FAIL);
                SetMainStatus(i18n::S_MS_VERIFY_NET_FAIL, true, err);
                ShowMainVerifyButtons(true, true);
                delete p;
                ApplyMainEnabledState();
                return 0;
            }
            if (!p->navIsLogin) {
                delete p;
                WipeSessionAndToLogin(i18n::S_NOTICE_CRED_INVALID);
                return 0;
            }

            // 登录态有效
            if (p->roomTransportOk && p->roomCode == 0 && p->roomId > 0) {
                g.roomId = std::to_string(p->roomId);
                g.roomKnown = true;
                SetWindowTextW(g.hRoom, W(g.roomId).c_str());
                if (g.justLoggedIn) {
                    SetMainStatus(i18n::S_MS_LOGIN_WELCOME, false);
                } else {
                    SetMainStatus(i18n::S_MS_AUTO_ENTER, false);
                }
                ShowMainVerifyButtons(false, false);
            } else if (p->roomTransportOk) {
                g.roomKnown = false;
                SetWindowTextW(g.hRoom, i18n::T(i18n::S_MAIN_ROOM_FAILED));
                std::wstring msg = W(p->error);
                if (msg.empty()) {
                    SetMainStatus(i18n::S_MS_ROOM_FAIL, true);
                } else {
                    SetMainStatus(i18n::S_MS_ROOM_FAIL_DETAIL, true, msg);
                }
                ShowMainVerifyButtons(true, true);
            } else {
                g.roomKnown = false;
                SetWindowTextW(g.hRoom, i18n::T(i18n::S_MAIN_ROOM_FAILED));
                std::wstring msg = W(p->error);
                if (msg.empty()) {
                    SetMainStatus(i18n::S_MS_ROOM_NET_FAIL, true);
                } else {
                    SetMainStatus(i18n::S_MS_ROOM_NET_FAIL_DETAIL, true, msg);
                }
                ShowMainVerifyButtons(true, true);
            }
            g.justLoggedIn = false;
            delete p;
            ApplyMainEnabledState();
            return 0;
        }

        case WM_APP_START: {
            StartPayload* p = (StartPayload*)lp;
            g.startInFlight = false;
            bili::StartLiveResult& r = p->result;

            if (!r.transportOk) {
                std::wstring err = W(r.error);
                if (err.empty()) err = i18n::T(i18n::S_MS_NET_FAIL);
                SetMainStatus(i18n::S_MS_START_NET_FAIL, true, err);
            } else if (r.needFaceAuth) {
                ShowMainRtmpBlock(false);
                if (p->faceQrOk) {
                    QrPanelSetContent(g.hFaceQr, p->faceModules);
                    ShowMainFaceBlock(true);
                    SetMainStatus(i18n::S_MS_FACE_NEEDED, false);
                } else {
                    ShowMainFaceBlock(false);
                    std::wstring err =
                        (p->faceQrErrorId != i18n::S_NONE) ? i18n::T(p->faceQrErrorId) : L"";
                    if (err.empty()) {
                        SetMainStatus(i18n::S_MS_FACE_QR_FAIL, true);
                    } else {
                        SetMainStatus(i18n::S_MS_FACE_QR_FAIL_DETAIL, true, err);
                    }
                }
            } else if (!r.rtmpAddr.empty() && !r.rtmpCode.empty()) {
                ShowMainFaceBlock(false);
                SetWindowTextW(g.hAddrEdit, W(r.rtmpAddr).c_str());
                SetWindowTextW(g.hCodeEdit, W(r.rtmpCode).c_str());
                ShowMainRtmpBlock(true);
                SetMainStatus(i18n::S_MS_LIVE_STARTED, false);
            } else {
                std::wstring msg = W(r.message);
                if (msg.empty()) msg = i18n::T(i18n::S_MS_NO_RTMP);
                if (r.code != 0) {
                    msg += i18n::F(i18n::S_MS_CODE_SUFFIX, std::to_wstring(r.code));
                }
                SetMainStatus(i18n::S_MS_START_FAIL, true, msg);
            }
            delete p;
            ApplyMainEnabledState();
            return 0;
        }

        case WM_APP_STOP: {
            SimpleApiPayload* p = (SimpleApiPayload*)lp;
            g.stopInFlight = false;
            bili::SimpleApiResult& r = p->result;
            if (!r.transportOk) {
                std::wstring err = W(r.error);
                if (err.empty()) err = i18n::T(i18n::S_MS_NET_FAIL);
                SetMainStatus(i18n::S_MS_STOP_NET_FAIL, true, err);
            } else if (r.code == 0) {
                ShowMainRtmpBlock(false);
                ShowMainFaceBlock(false);
                SetWindowTextW(g.hAddrEdit, L"");
                SetWindowTextW(g.hCodeEdit, L"");
                SetMainStatus(i18n::S_MS_STOPPED, false);
            } else {
                std::wstring msg = W(r.message);
                if (msg.empty()) msg = i18n::T(i18n::S_MS_UNKNOWN_ERR);
                SetMainStatus(i18n::S_MS_STOP_FAIL, true, msg, std::to_wstring(r.code));
            }
            delete p;
            ApplyMainEnabledState();
            return 0;
        }

        case WM_APP_LOGOUT: {
            SimpleApiPayload* p = (SimpleApiPayload*)lp;
            g.logoutInFlight = false;
            bool netFail = !p->result.transportOk;
            std::wstring err = W(p->result.error);
            int code = p->result.code;
            delete p;
            ApplyMainEnabledState();

            if (!netFail && code == 0) {
                WipeSessionAndToLogin(i18n::S_NOTICE_LOGGED_OUT);
                return 0;
            }
            if (netFail) {
                std::wstring msg = err.empty() ? i18n::T(i18n::S_MS_NET_FAIL) : err;
                int rc = MessageBoxW(hwnd, i18n::F(i18n::S_MB_LOGOUT_NET_FAIL, msg).c_str(),
                                     i18n::T(i18n::S_MAIN_LOGOUT), MB_ICONWARNING | MB_YESNO);
                if (rc == IDYES) {
                    WipeSessionAndToLogin(i18n::S_NOTICE_LOCAL_WIPED);
                } else {
                    SetMainStatus(i18n::S_MS_LOGOUT_INCOMPLETE, true);
                }
                return 0;
            }
            // 服务端拒绝（如凭证本就失效）：用户的登出意图明确，仍执行本地销毁
            WipeSessionAndToLogin(i18n::S_NOTICE_SERVER_INVALID);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            HWND ctl = (HWND)lp;
            int id = GetDlgCtrlID(ctl);
            SetBkMode(hdc, TRANSPARENT);
            SetBkColor(hdc, RGB(255, 255, 255));
            if (id == IDC_MAIN_STATUS) {
                SetTextColor(hdc, g.mainStatus.err ? RGB(200, 32, 32) : RGB(37, 99, 235));
            } else if (id == IDC_MAIN_HINT || id == IDC_MAIN_FOOTER) {
                SetTextColor(hdc, RGB(120, 120, 120));
            } else {
                SetTextColor(hdc, RGB(30, 30, 30));
            }
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            g.mainWnd = nullptr;
            g.hRoom = g.hPartLabel = g.hCombo = g.hStart = g.hStop = g.hLogout = nullptr;
            g.hMainLang = g.hStatus = g.hRetry = g.hRelogin = g.hFooter = nullptr;
            g.hFaceTitle = g.hFaceQr = g.hFaceCancel = nullptr;
            g.hLabelAddr = g.hAddrEdit = g.hCopyAddr = nullptr;
            g.hLabelCode = g.hCodeEdit = g.hCopyCode = g.hHint = nullptr;
            g.verifyInFlight = g.startInFlight = g.stopInFlight = g.logoutInFlight = false;
            if (!g.loginWnd) PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 按当前语言刷新主窗口的全部文本（语言切换时调用）
void RefreshMainWindowText(HWND hwnd) {
    SetWindowTextW(hwnd, i18n::T(i18n::S_APP_TITLE_MAIN));
    SetDlgItemTextW(hwnd, IDC_MAIN_TITLE, i18n::T(i18n::S_APP_TITLE_MAIN));
    SetDlgItemTextW(hwnd, IDC_MAIN_LOGOUT, i18n::T(i18n::S_MAIN_LOGOUT));
    SetDlgItemTextW(hwnd, IDC_MAIN_LANG, i18n::LangSwitchLabel());
    SetDlgItemTextW(hwnd, IDC_MAIN_ROOM_LABEL, i18n::T(i18n::S_MAIN_ROOM_LABEL));
    SetDlgItemTextW(hwnd, IDC_MAIN_PART_LABEL, i18n::T(i18n::S_MAIN_PART_LABEL));
    SetDlgItemTextW(hwnd, IDC_MAIN_START, i18n::T(i18n::S_MAIN_START));
    SetDlgItemTextW(hwnd, IDC_MAIN_STOP, i18n::T(i18n::S_MAIN_STOP));
    SetDlgItemTextW(hwnd, IDC_MAIN_RETRY, i18n::T(i18n::S_MAIN_RETRY));
    SetDlgItemTextW(hwnd, IDC_MAIN_RELOGIN, i18n::T(i18n::S_MAIN_RELOGIN));
    SetDlgItemTextW(hwnd, IDC_MAIN_LABEL_ADDR, i18n::T(i18n::S_MAIN_LABEL_ADDR));
    SetDlgItemTextW(hwnd, IDC_MAIN_LABEL_CODE, i18n::T(i18n::S_MAIN_LABEL_CODE));
    SetDlgItemTextW(hwnd, IDC_MAIN_HINT, i18n::T(i18n::S_MAIN_HINT));
    SetDlgItemTextW(hwnd, IDC_MAIN_FACE_TITLE, i18n::T(i18n::S_MAIN_FACE_TITLE));
    SetDlgItemTextW(hwnd, IDC_MAIN_FACE_CANCEL, i18n::T(i18n::S_MAIN_FACE_CANCEL));
    SetDlgItemTextW(hwnd, IDC_MAIN_FOOTER, i18n::T(i18n::S_MAIN_FOOTER));

    // 复制按钮：取消"已复制"倒计时，恢复为普通文本
    KillTimer(hwnd, TIMER_COPY_ADDR);
    KillTimer(hwnd, TIMER_COPY_CODE);
    SetDlgItemTextW(hwnd, IDC_MAIN_COPY_ADDR, i18n::T(i18n::S_MAIN_COPY));
    SetDlgItemTextW(hwnd, IDC_MAIN_COPY_CODE, i18n::T(i18n::S_MAIN_COPY));

    // 房间号（动态内容不翻译）
    if (g.hRoom && IsWindow(g.hRoom)) {
        if (g.roomKnown) {
            SetWindowTextW(g.hRoom, W(g.roomId).c_str());
        } else {
            SetWindowTextW(g.hRoom, i18n::T(g.verifyInFlight ? i18n::S_MAIN_ROOM_FETCHING
                                                             : i18n::S_MAIN_ROOM_FAILED));
        }
    }

    // 状态栏：按新语言重新渲染模板
    if (g.hStatus && IsWindow(g.hStatus)) {
        SetWindowTextW(g.hStatus, i18n::F(g.mainStatus.key, g.mainStatus.a1, g.mainStatus.a2).c_str());
        InvalidateRect(g.hStatus, nullptr, TRUE);
    }
    if (g.hFaceQr && IsWindow(g.hFaceQr)) InvalidateRect(g.hFaceQr, nullptr, TRUE);
}
