// main.cpp - 入口、共享 UI 助手、二维码面板控件、自检 / 调试模式
#include "app.h"
#include "json.h"
#include "qrcode.h"

#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

AppState g;

// 其他文件中的窗口过程
LRESULT CALLBACK LoginWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);

const wchar_t* kQrPanelClass = L"BiliCppQrPanel";
const wchar_t* kLoginWndClass = L"BiliLiveLoginWnd";
const wchar_t* kMainWndClass = L"BiliLiveMainWnd";

// ---------------- DPI 缩放与字体 ----------------
namespace {
int g_dpi = 96;
}

HFONT g_fontUI = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontSmall = nullptr;

int S(int v) { return MulDiv(v, g_dpi, 96); }

void InitUI(HINSTANCE hInst) {
    g.hInst = hInst;
    SetProcessDPIAware();
    HDC hdc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    if (g_dpi <= 0) g_dpi = 96;

    auto makeFont = [&](int pt, int weight) {
        return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    };
    g_fontUI = makeFont(10, FW_NORMAL);
    g_fontTitle = makeFont(15, FW_BOLD);
    g_fontSmall = makeFont(9, FW_NORMAL);
}

void DestroyUIFonts() {
    if (g_fontUI) DeleteObject(g_fontUI);
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontSmall) DeleteObject(g_fontSmall);
    g_fontUI = g_fontTitle = g_fontSmall = nullptr;
}

// ---------------- 窗口辅助 ----------------
void CenterWindowOnScreen(HWND hwnd) {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd, nullptr, (sw - w) / 2, (sh - h) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void ResizeClientToSize(HWND hwnd, int cx, int cy) {
    DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    RECT rc = {0, 0, cx, cy};
    AdjustWindowRect(&rc, style, FALSE);
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---------------- 二维码面板控件 ----------------
namespace {

struct QrPanel {
    std::vector<std::vector<bool>> modules;  // 空 = 无内容
    HBITMAP dib = nullptr;
    void* bits = nullptr;
    int dim = 0;  // 含 4 模块静默边的边长

    ~QrPanel() {
        if (dib) DeleteObject(dib);
    }
};

std::map<HWND, QrPanel*> g_qrPanels;

void RebuildQrDib(QrPanel* p) {
    if (p->dib) {
        DeleteObject(p->dib);
        p->dib = nullptr;
        p->bits = nullptr;
    }
    p->dim = 0;
    int n = (int)p->modules.size();
    if (n <= 0) return;

    int dim = n + 8;  // 四周各 4 模块静默区
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = dim;
    bmi.bmiHeader.biHeight = -dim;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        return;
    }
    p->dib = dib;
    p->bits = bits;
    p->dim = dim;

    const uint32_t white = 0xFFFFFFFFu, black = 0xFF000000u;
    uint32_t* px = (uint32_t*)bits;
    for (int y = 0; y < dim; ++y) {
        for (int x = 0; x < dim; ++x) {
            bool dark = false;
            int my = y - 4, mx = x - 4;
            if (my >= 0 && my < n && mx >= 0 && mx < n) {
                dark = p->modules[(size_t)my][(size_t)mx];
            }
            px[(size_t)y * dim + x] = dark ? black : white;
        }
    }
}

LRESULT CALLBACK QrPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            auto it = g_qrPanels.find(hwnd);
            if (it != g_qrPanels.end() && it->second->dib && it->second->dim > 0) {
                QrPanel* p = it->second;
                int w = rc.right, h = rc.bottom;
                int cell = std::min(w, h) / p->dim;
                if (cell < 1) cell = 1;
                int dst = cell * p->dim;
                int ox = (w - dst) / 2, oy = (h - dst) / 2;
                HDC mem = CreateCompatibleDC(hdc);
                HGDIOBJ old = SelectObject(mem, p->dib);
                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchBlt(hdc, ox, oy, dst, dst, mem, 0, 0, p->dim, p->dim, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            } else {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(160, 160, 160));
                DrawTextW(hdc, i18n::T(i18n::S_QR_LOADING), -1, &rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY: {
            auto it = g_qrPanels.find(hwnd);
            if (it != g_qrPanels.end()) {
                delete it->second;
                g_qrPanels.erase(it);
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void QrPanelSetContent(HWND panel, const std::vector<std::vector<bool>>& modules) {
    if (!panel || !IsWindow(panel)) return;
    QrPanel*& slot = g_qrPanels[panel];
    if (!slot) slot = new QrPanel();
    slot->modules = modules;
    RebuildQrDib(slot);
    InvalidateRect(panel, nullptr, TRUE);
}

void QrPanelClear(HWND panel) {
    if (!panel) return;
    auto it = g_qrPanels.find(panel);
    if (it == g_qrPanels.end()) return;
    it->second->modules.clear();
    RebuildQrDib(it->second);
    InvalidateRect(panel, nullptr, TRUE);
}

// ---------------- 类注册与窗口切换 ----------------
void RegisterAppClasses(HINSTANCE hInst) {
    WNDCLASSW wc = {};
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    wc.lpfnWndProc = QrPanelProc;
    wc.lpszClassName = kQrPanelClass;
    RegisterClassW(&wc);

    wc.lpfnWndProc = LoginWndProc;
    wc.lpszClassName = kLoginWndClass;
    RegisterClassW(&wc);

    wc.lpfnWndProc = MainWndProc;
    wc.lpszClassName = kMainWndClass;
    RegisterClassW(&wc);
}

void SwitchToMainWindow() {
    if (!g.mainWnd) CreateMainWindow();
    if (g.loginWnd) DestroyWindow(g.loginWnd);
}

void SwitchToLoginWindow() {
    if (!g.loginWnd) CreateLoginWindow();
    if (g.mainWnd) DestroyWindow(g.mainWnd);
}

void WipeSessionAndToLogin(i18n::StrId notice) {
    session::Wipe();
    util::SecureZeroString(g.session.cookie);
    g.session = SessionData();
    g.sessionValid = false;
    if (notice != i18n::S_NONE) {
        MessageBoxW(nullptr, i18n::T(notice), i18n::T(i18n::S_MB_INFO_TITLE),
                    MB_ICONINFORMATION | MB_OK);
    }
    SwitchToLoginWindow();
}

// 切换语言：持久化 + 按新语言重渲染两个窗口
void SwitchLanguageAndRefresh() {
    i18n::SetLang(i18n::GetLang() == i18n::Lang::En ? i18n::Lang::Zh : i18n::Lang::En);
    i18n::SaveLangSetting();
    if (g.loginWnd && IsWindow(g.loginWnd)) RefreshLoginWindowText(g.loginWnd);
    if (g.mainWnd && IsWindow(g.mainWnd)) RefreshMainWindowText(g.mainWnd);
}

// ---------------- 状态文本与控件可见性 ----------------
void SetLoginStatus(i18n::StrId key, bool isError, const std::wstring& a1, const std::wstring& a2) {
    g.loginStatus.key = key;
    g.loginStatus.a1 = a1;
    g.loginStatus.a2 = a2;
    g.loginStatus.err = isError;
    if (g.hLoginStatus && IsWindow(g.hLoginStatus)) {
        SetWindowTextW(g.hLoginStatus, i18n::F(key, a1, a2).c_str());
        InvalidateRect(g.hLoginStatus, nullptr, TRUE);
    }
}

void SetMainStatus(i18n::StrId key, bool isError, const std::wstring& a1, const std::wstring& a2) {
    g.mainStatus.key = key;
    g.mainStatus.a1 = a1;
    g.mainStatus.a2 = a2;
    g.mainStatus.err = isError;
    if (g.hStatus && IsWindow(g.hStatus)) {
        SetWindowTextW(g.hStatus, i18n::F(key, a1, a2).c_str());
        InvalidateRect(g.hStatus, nullptr, TRUE);
    }
}

void ApplyMainEnabledState() {
    bool base = g.sessionValid && !g.verifyInFlight;
    bool busy = g.startInFlight || g.stopInFlight || g.logoutInFlight;
    if (g.hCombo) EnableWindow(g.hCombo, base && !busy);
    if (g.hStart) EnableWindow(g.hStart, base && !busy && g.roomKnown);
    if (g.hStop) EnableWindow(g.hStop, base && !busy);
    if (g.hLogout) EnableWindow(g.hLogout, base && !busy);
}

namespace {
void ShowCtl(HWND h, bool show) {
    if (h) ShowWindow(h, show ? SW_SHOW : SW_HIDE);
}
}  // namespace

void ShowMainVerifyButtons(bool showRetry, bool showRelogin) {
    ShowCtl(g.hRetry, showRetry);
    ShowCtl(g.hRelogin, showRelogin);
}

void ShowMainRtmpBlock(bool show) {
    ShowCtl(g.hLabelAddr, show);
    ShowCtl(g.hAddrEdit, show);
    ShowCtl(g.hCopyAddr, show);
    ShowCtl(g.hLabelCode, show);
    ShowCtl(g.hCodeEdit, show);
    ShowCtl(g.hCopyCode, show);
    ShowCtl(g.hHint, show);
}

void ShowMainFaceBlock(bool show) {
    ShowCtl(g.hFaceTitle, show);
    ShowCtl(g.hFaceQr, show);
    ShowCtl(g.hFaceCancel, show);
}

// ---------------- 调试模式：--qr-dump <outFile> <L|M|Q|H> <auto|0-7> <text...> ----------------
namespace {

int RunQrDump(int argc, LPWSTR* argv) {
    if (argc < 6) return 2;
    std::wstring outPath = argv[2];
    std::string ecc = util::WideToUtf8(argv[3]);
    std::string maskS = util::WideToUtf8(argv[4]);
    std::string text;
    for (int i = 5; i < argc; ++i) {
        if (i > 5) text += " ";
        text += util::WideToUtf8(argv[i]);
    }
    qrc::Ecc e = qrc::Ecc::M;
    if (ecc == "L") e = qrc::Ecc::L;
    else if (ecc == "Q") e = qrc::Ecc::Q;
    else if (ecc == "H") e = qrc::Ecc::H;
    int forceMask = -1;
    if (maskS != "auto") forceMask = atoi(maskS.c_str());

    std::vector<std::vector<bool>> mods;
    int ver = 0, mask = -1;
    if (!qrc::EncodeText(text, e, mods, ver, forceMask, &mask)) return 3;

    std::string out = "version=" + std::to_string(ver) + " mask=" + std::to_string(mask) +
                      " size=" + std::to_string(mods.size()) + "\r\n";
    for (const auto& row : mods) {
        for (bool b : row) out.push_back(b ? '1' : '0');
        out += "\r\n";
    }
    HANDLE f = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 4;
    DWORD written = 0;
    WriteFile(f, out.data(), (DWORD)out.size(), &written, nullptr);
    CloseHandle(f);
    return 0;
}

// ---------------- 调试模式：--selftest <outFile> ----------------
int RunSelfTest(int argc, LPWSTR* argv) {
    std::wstring outPath = (argc >= 3) ? argv[2] : L"selftest_result.txt";
    std::string report;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        report += std::string(ok ? "PASS: " : "FAIL: ") + name + "\r\n";
        if (!ok) ++failures;
    };

    // 1. URL 编码
    check(util::UrlEncode("a b+c/d?e=f&g") == "a%20b%2Bc%2Fd%3Fe%3Df%26g", "urlencode-basic");
    check(util::UrlEncode("\xE4\xB8\xAD\xE6\x96\x87") == "%E4%B8%AD%E6%96%87", "urlencode-utf8");

    // 2. JSON
    {
        std::string js =
            R"({"code":0,"message":"ok","data":{"room_id":12345,"name":"abc","isLogin":true},"arr":[1,2,3]})";
        JsonValue v;
        check(JsonValue::Parse(js, v), "json-parse");
        const JsonValue* code = v.Find("code");
        check(code && code->AsInt(-1) == 0, "json-number");
        const JsonValue* data = v.Find("data");
        check(data && data->Find("room_id") && data->Find("room_id")->AsInt(0) == 12345, "json-nested");
        check(data && data->Find("isLogin") && data->Find("isLogin")->AsBool(false), "json-bool");
        const JsonValue* arr = v.Find("arr");
        check(arr && arr->arrayValue.size() == 3 && arr->arrayValue[0].AsInt() == 1, "json-array");
    }
    {
        JsonValue u;
        bool ok = JsonValue::Parse("{\"m\":\"\\u4e2d\\u6587\\ud83d\\ude00\"}", u);
        std::string expect = "\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80";
        check(ok && u.Find("m") && u.Find("m")->AsString() == expect, "json-unicode-escape");
    }

    // 3. QR 基础性质（详细正确性由 tools/verify_qr.js 与 npm qrcode 对照）
    {
        std::vector<std::vector<bool>> mods;
        int ver = 0, mask = -1;
        check(qrc::EncodeText("HELLO WORLD", qrc::Ecc::M, mods, ver, -1, &mask), "qr-encode");
        check(ver == 1 && mods.size() == 21, "qr-v1-size");
        // 定位图形抽查：左上角 7x7 的角应为深色
        bool finderOk = mods.size() == 21 && mods[0][0] && mods[0][6] && mods[6][0] && !mods[1][1] &&
                        mods[2][2];
        check(finderOk, "qr-finder-pattern");
    }

    // 4. Cookie 解析
    {
        std::string jar = "SESSDATA=abc%2Cdef; bili_jct=xyz; DedeUserID=42";
        check(util::CookieValue(jar, "SESSDATA") == "abc%2Cdef", "cookie-value-sessdata");
        check(util::CookieValue(jar, "DedeUserID") == "42", "cookie-value-uid");
        check(util::CookieValue(jar, "missing").empty(), "cookie-value-missing");
    }

    // 5. 会话加密存储 + 篡改销毁
    {
        SessionData sd;
        sd.uid = "42";
        sd.cookie = "SESSDATA=abc; bili_jct=xyz; DedeUserID=42";
        sd.savedAt = 1;
        check(session::Validate(sd), "session-validate");
        SessionData bad = sd;
        bad.cookie = "SESSDATA=abc; bili_jct=xyz; DedeUserID=43";
        check(!session::Validate(bad), "session-validate-mismatch");

        check(session::Save(sd), "session-save");
        SessionData loaded;
        bool existed = false;
        check(session::Load(loaded, existed) && existed && loaded.uid == "42" &&
                  loaded.cookie == sd.cookie,
              "session-load");

        // 篡改文件后必须解密失败并被销毁
        std::wstring path = util::JoinPath(util::ExeDir(), session::kFileName);
        HANDLE f = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        bool opened = (f != INVALID_HANDLE_VALUE);
        bool tampered = false;
        if (opened) {
            // 翻转文件最后一个字节（DPAPI blob 的 HMAC 签名尾部），必须被完整性校验捕获
            BYTE b = 0;
            DWORD rd = 0;
            SetFilePointer(f, -1, nullptr, FILE_END);
            if (ReadFile(f, &b, 1, &rd, nullptr) && rd == 1) {
                b ^= 0xFF;
                DWORD wr = 0;
                SetFilePointer(f, -1, nullptr, FILE_END);
                tampered = WriteFile(f, &b, 1, &wr, nullptr) && wr == 1;
            }
            CloseHandle(f);
        }
        SessionData t;
        bool ex2 = false;
        bool loadFail = !session::Load(t, ex2);
        bool gone = !session::Exists();
        check(opened, "session-tamper-open");
        check(tampered, "session-tamper-write");
        check(loadFail, "session-tamper-loadfail");
        check(ex2, "session-tamper-existed");
        check(gone, "session-tamper-gone");
    }

    // 6. 多语言：两表完整、模板渲染、语言设置持久化往返
    {
        check(i18n::CheckTablesComplete(), "i18n-tables-complete");
        std::wstring s = i18n::F(i18n::S_LS_WAIT_CODE, L"123");
        check(s.find(L"123") != std::wstring::npos && s.find(L"%1") == std::wstring::npos,
              "i18n-format-placeholder");
        i18n::Lang orig = i18n::GetLang();
        i18n::SetLang(i18n::Lang::En);
        check(i18n::SaveLangSetting(), "i18n-save-en");
        i18n::SetLang(i18n::Lang::Zh);
        i18n::LoadLangSetting();
        check(i18n::GetLang() == i18n::Lang::En, "i18n-roundtrip-en");
        i18n::SetLang(i18n::Lang::Zh);
        check(i18n::SaveLangSetting(), "i18n-save-zh");
        i18n::SetLang(i18n::Lang::En);
        i18n::LoadLangSetting();
        check(i18n::GetLang() == i18n::Lang::Zh, "i18n-roundtrip-zh");
        i18n::SetLang(orig);
        i18n::SaveLangSetting();  // 恢复原设置
    }

    report += failures == 0 ? "RESULT: ALL PASS\r\n" : "RESULT: FAILURES=" + std::to_string(failures) + "\r\n";

    HANDLE f = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(f, report.data(), (DWORD)report.size(), &written, nullptr);
        CloseHandle(f);
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace

// ---------------- 入口 ----------------
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) {
        std::wstring a1 = argv[1];
        if (a1 == L"--qr-dump") {
            int rc = RunQrDump(argc, argv);
            LocalFree(argv);
            return rc;
        }
        if (a1 == L"--selftest") {
            int rc = RunSelfTest(argc, argv);
            LocalFree(argv);
            return rc;
        }
    }
    if (argv) LocalFree(argv);

    InitUI(hInst);
    i18n::LoadLangSetting();  // 语言偏好（默认英文）
    RegisterAppClasses(hInst);

    // 启动时加载本地会话：有效则免扫码直进控制台
    SessionData s;
    bool existed = false;
    g.sessionValid = session::Load(s, existed);
    if (g.sessionValid) {
        g.session = s;
        CreateMainWindow();
    } else {
        CreateLoginWindow();
        if (existed) {
            MessageBoxW(nullptr, i18n::T(i18n::S_MB_BROKEN_SESSION), i18n::T(i18n::S_MB_INFO_TITLE),
                        MB_ICONINFORMATION | MB_OK);
        }
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    util::SecureZeroString(g.session.cookie);
    DestroyUIFonts();
    return 0;
}
