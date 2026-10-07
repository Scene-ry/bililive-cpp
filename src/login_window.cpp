// login_window.cpp - 扫码登录窗口（对应原 /tools/bililive/login 页面）
#include "app.h"
#include "qrcode.h"

#include <thread>

namespace {

const int IDC_LOGIN_TITLE = 2001;
const int IDC_LOGIN_QR = 2002;
const int IDC_LOGIN_STATUS = 2003;
const int IDC_LOGIN_REFRESH = 2004;
const int IDC_LOGIN_HINT = 2005;
const int IDC_LOGIN_LANG = 2006;

const UINT_PTR TIMER_LOGIN_POLL = 1;
const UINT kPollIntervalMs = 3000;

void SetFontTo(HWND hwnd, int id, HFONT font) {
    HWND ctl = GetDlgItem(hwnd, id);
    if (ctl) SendMessageW(ctl, WM_SETFONT, (WPARAM)font, TRUE);
}

void ApplyFonts(HWND hwnd) {
    SetFontTo(hwnd, IDC_LOGIN_TITLE, g_fontTitle);
    SetFontTo(hwnd, IDC_LOGIN_QR, g_fontUI);
    SetFontTo(hwnd, IDC_LOGIN_STATUS, g_fontUI);
    SetFontTo(hwnd, IDC_LOGIN_REFRESH, g_fontUI);
    SetFontTo(hwnd, IDC_LOGIN_LANG, g_fontUI);
    SetFontTo(hwnd, IDC_LOGIN_HINT, g_fontSmall);
}

}  // namespace

// 请求生成二维码并在后台编码（网络 + QR 编码均不阻塞 UI 线程）
void LoginStartGenerate(HWND hwnd) {
    if (g.generateInFlight) return;
    g.generateInFlight = true;
    g.pollInFlight = false;
    KillTimer(hwnd, TIMER_LOGIN_POLL);
    g.qrcodeKey.clear();
    QrPanelClear(g.hLoginQr);
    if (g.hLoginRefresh) EnableWindow(g.hLoginRefresh, FALSE);
    SetLoginStatus(i18n::S_LS_GENERATING, false);

    std::thread([hwnd]() {
        LoginQrPayload* p = new LoginQrPayload();
        bili::QrGenerateResult gen = bili::QrGenerate();
        p->ok = gen.ok;
        p->error = gen.error;
        p->url = gen.url;
        p->qrcodeKey = gen.qrcodeKey;
        if (gen.ok) {
            int ver = 0;
            if (qrc::EncodeText(gen.url, qrc::Ecc::M, p->modules, ver)) {
                p->qrOk = true;
                p->version = ver;
            } else {
                p->qrErrorId = i18n::S_QR_TOO_LONG;
            }
        }
        PostPayload(hwnd, WM_APP_LOGIN_QR, p);
    }).detach();
}

HWND CreateLoginWindow() {
    HWND hwnd = CreateWindowExW(0, kLoginWndClass, i18n::T(i18n::S_APP_TITLE_LOGIN),
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0, 400,
                                300, nullptr, nullptr, g.hInst, nullptr);
    if (!hwnd) return nullptr;
    ResizeClientToSize(hwnd, S(420), S(560));
    CenterWindowOnScreen(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return hwnd;
}

LRESULT CALLBACK LoginWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g.loginWnd = hwnd;
            g.generateInFlight = false;
            g.pollInFlight = false;
            g.qrcodeKey.clear();
            g.loginStatus = StatusView();  // 重建窗口时重置状态

            CreateWindowExW(0, L"STATIC", i18n::T(i18n::S_APP_TITLE_LOGIN),
                            WS_CHILD | WS_VISIBLE | SS_CENTER, S(20), S(18), S(380), S(30), hwnd,
                            (HMENU)IDC_LOGIN_TITLE, g.hInst, nullptr);
            g.hLoginQr = CreateWindowExW(0, kQrPanelClass, L"", WS_CHILD | WS_VISIBLE, S(60), S(80),
                                         S(300), S(300), hwnd, (HMENU)IDC_LOGIN_QR, g.hInst, nullptr);
            g.hLoginStatus = CreateWindowExW(0, L"STATIC", L"",
                                             WS_CHILD | WS_VISIBLE | SS_CENTER, S(20), S(396), S(380),
                                             S(48), hwnd, (HMENU)IDC_LOGIN_STATUS, g.hInst, nullptr);
            g.hLoginRefresh = CreateWindowExW(0, L"BUTTON", i18n::T(i18n::S_LOGIN_REFRESH),
                                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, S(150), S(456),
                                              S(120), S(32), hwnd, (HMENU)IDC_LOGIN_REFRESH, g.hInst,
                                              nullptr);
            CreateWindowExW(0, L"STATIC", i18n::T(i18n::S_LOGIN_HINT),
                            WS_CHILD | WS_VISIBLE | SS_CENTER, S(20), S(502), S(380), S(44), hwnd,
                            (HMENU)IDC_LOGIN_HINT, g.hInst, nullptr);
            g.hLoginLang = CreateWindowExW(0, L"BUTTON", i18n::LangSwitchLabel(),
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, S(320), S(18),
                                           S(80), S(28), hwnd, (HMENU)IDC_LOGIN_LANG, g.hInst, nullptr);

            ApplyFonts(hwnd);
            SetLoginStatus(i18n::S_LS_GENERATING, false);
            LoginStartGenerate(hwnd);
            return 0;
        }

        case WM_COMMAND: {
            if (LOWORD(wp) == IDC_LOGIN_REFRESH && HIWORD(wp) == BN_CLICKED) {
                LoginStartGenerate(hwnd);
            } else if (LOWORD(wp) == IDC_LOGIN_LANG && HIWORD(wp) == BN_CLICKED) {
                SwitchLanguageAndRefresh();
            }
            return 0;
        }

        case WM_TIMER: {
            if (wp == TIMER_LOGIN_POLL) {
                if (g.pollInFlight || g.generateInFlight || g.qrcodeKey.empty()) return 0;
                g.pollInFlight = true;
                std::string key = g.qrcodeKey;
                std::thread([hwnd, key]() {
                    PollPayload* p = new PollPayload();
                    p->result = bili::QrPoll(key);
                    PostPayload(hwnd, WM_APP_POLL, p);
                }).detach();
            }
            return 0;
        }

        case WM_APP_LOGIN_QR: {
            LoginQrPayload* p = (LoginQrPayload*)lp;
            g.generateInFlight = false;
            if (p->ok && p->qrOk) {
                g.qrcodeKey = p->qrcodeKey;
                QrPanelSetContent(g.hLoginQr, p->modules);
                SetLoginStatus(i18n::S_LS_SCAN_PROMPT, false);
                if (g.hLoginRefresh) EnableWindow(g.hLoginRefresh, TRUE);
                SetTimer(hwnd, TIMER_LOGIN_POLL, kPollIntervalMs, nullptr);
            } else {
                std::wstring err;
                if (!p->ok) {
                    err = W(p->error);  // 网络 / 服务端错误
                } else if (p->qrErrorId != i18n::S_NONE) {
                    err = i18n::T(p->qrErrorId);
                }
                if (err.empty()) err = i18n::T(i18n::S_MS_UNKNOWN_ERR);
                SetLoginStatus(i18n::S_LS_GEN_FAILED, true, err);
                if (g.hLoginRefresh) EnableWindow(g.hLoginRefresh, TRUE);
            }
            delete p;
            return 0;
        }

        case WM_APP_POLL: {
            PollPayload* p = (PollPayload*)lp;
            g.pollInFlight = false;
            bili::QrPollResult& r = p->result;

            if (!r.transportOk) {
                SetLoginStatus(i18n::S_LS_NET_RETRY, true);
                delete p;
                return 0;
            }
            if (r.dataCode == 0) {
                // 登录成功：提取凭证并加密保存
                KillTimer(hwnd, TIMER_LOGIN_POLL);
                SessionData sd;
                sd.uid = r.uid;
                sd.cookie = r.cookieJar;
                sd.savedAt = util::UnixNow();
                if (session::Validate(sd)) {
                    bool saved = session::Save(sd);
                    g.session = sd;
                    g.sessionValid = true;
                    g.justLoggedIn = true;
                    delete p;
                    if (!saved) {
                        MessageBoxW(hwnd, i18n::T(i18n::S_MB_SAVE_FAILED),
                                    i18n::T(i18n::S_MB_INFO_TITLE), MB_ICONWARNING | MB_OK);
                    }
                    SwitchToMainWindow();
                } else {
                    SetLoginStatus(i18n::S_LS_CRED_INCOMPLETE, true);
                    delete p;
                    LoginStartGenerate(hwnd);
                }
                return 0;
            }
            if (r.dataCode == 86038) {
                KillTimer(hwnd, TIMER_LOGIN_POLL);
                SetLoginStatus(i18n::S_LS_EXPIRED, false);
                delete p;
                LoginStartGenerate(hwnd);
                return 0;
            }
            if (r.dataCode == 86090) {
                SetLoginStatus(i18n::S_LS_SCANNED, false);
            } else if (r.dataCode == 86101) {
                // 未扫码，保持等待
            } else {
                SetLoginStatus(i18n::S_LS_WAIT_CODE, false, std::to_wstring(r.dataCode));
            }
            delete p;
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            HWND ctl = (HWND)lp;
            int id = GetDlgCtrlID(ctl);
            SetBkMode(hdc, TRANSPARENT);
            if (id == IDC_LOGIN_STATUS) {
                SetTextColor(hdc, g.loginStatus.err ? RGB(200, 32, 32) : RGB(37, 99, 235));
            } else if (id == IDC_LOGIN_HINT) {
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
            g.loginWnd = nullptr;
            g.hLoginQr = nullptr;
            g.hLoginStatus = nullptr;
            g.hLoginRefresh = nullptr;
            g.hLoginLang = nullptr;
            g.qrcodeKey.clear();
            g.generateInFlight = false;
            g.pollInFlight = false;
            if (!g.mainWnd) PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 按当前语言刷新登录窗口的全部文本（语言切换时调用）
void RefreshLoginWindowText(HWND hwnd) {
    SetWindowTextW(hwnd, i18n::T(i18n::S_APP_TITLE_LOGIN));
    SetDlgItemTextW(hwnd, IDC_LOGIN_TITLE, i18n::T(i18n::S_APP_TITLE_LOGIN));
    SetDlgItemTextW(hwnd, IDC_LOGIN_REFRESH, i18n::T(i18n::S_LOGIN_REFRESH));
    SetDlgItemTextW(hwnd, IDC_LOGIN_HINT, i18n::T(i18n::S_LOGIN_HINT));
    SetDlgItemTextW(hwnd, IDC_LOGIN_LANG, i18n::LangSwitchLabel());
    if (g.hLoginStatus && IsWindow(g.hLoginStatus)) {
        SetWindowTextW(g.hLoginStatus,
                       i18n::F(g.loginStatus.key, g.loginStatus.a1, g.loginStatus.a2).c_str());
        InvalidateRect(g.hLoginStatus, nullptr, TRUE);
    }
    if (g.hLoginQr && IsWindow(g.hLoginQr)) InvalidateRect(g.hLoginQr, nullptr, TRUE);
}
