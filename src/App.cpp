#include "App.h"

#include "UiIds.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <cwchar>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_MICA_EFFECT
#define DWMWA_MICA_EFFECT 1029
#endif

namespace {
constexpr wchar_t kAppTitle[] = L"Aria2DM";
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayMenuToggle = 0xA001;
constexpr UINT kTrayMenuExit = 0xA002;

int ScaleInt(int value, UINT dpi) {
    return MulDiv(value, (int)dpi, 96);
}
} // namespace

void AppLog(const std::wstring& folder, const std::wstring& text) {
    if (folder.empty()) return;
    std::wstring path = folder + L"\\win.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    std::wstring line = text + L"\r\n";
    DWORD written = 0;
    WriteFile(file, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

App::App(HINSTANCE instance) : instance_(instance) {
    loadSettings();
}

App::~App() {
    stopResolveWorker();
    megaProxy_.stop();
    aria_.stop();
    if (hwnd_) {
        KillTimer(hwnd_, 1);
        hwnd_ = nullptr;
    }
}

void App::queueStartupUrl(const std::wstring& url) {
    if (!url.empty()) startupUrl_ = url;
}

bool App::create(int showCommand) {
    dataDir_ = util::LocalAppData() + L"\\Aria2DM";
    if (dataDir_.empty() || dataDir_ == L"\\Aria2DM") dataDir_ = util::ExeDir() + L"\\data";
    util::EnsureDir(dataDir_);
    settingsPath_ = dataDir_ + L"\\settings.ini";
    loadSettings();
    sidebarWidth_ =
        settings_.sidebarCollapsed ? (float)kSidebarCompactWidth : (float)kSidebarWidth;
    applyStartupSetting();

    applyTheme();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &App::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kAppWindowClass;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) return false;

    UINT systemDpi = GetDpiForSystem();
    if (systemDpi == 0) systemDpi = 96;
    dpi_ = (float)systemDpi;

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenW = workArea.right - workArea.left;
    int screenH = workArea.bottom - workArea.top;
    int desiredW = ScaleInt(1180, systemDpi);
    int desiredH = ScaleInt(780, systemDpi);
    if (screenW > 0) desiredW = std::min(desiredW, (int)(screenW * 0.92));
    if (screenH > 0) desiredH = std::min(desiredH, (int)(screenH * 0.92));

    // Centre inside the work area so the frameless window never hides under the
    // taskbar (CW_USEDEFAULT can place the bottom edge at the screen edge).
    int posX = workArea.left + (screenW > 0 ? (screenW - desiredW) / 2 : 0);
    int posY = workArea.top + (screenH > 0 ? (screenH - desiredH) / 2 : 0);

    // The window is frameless, so the client area is the size we ask for.
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP;

    HWND hwnd = CreateWindowExW(exStyle, kAppWindowClass, kAppTitle, style, posX, posY,
                                desiredW, desiredH, nullptr, nullptr, instance_, this);
    if (!hwnd) return false;
    hwnd_ = hwnd;

    // Keep WS_CAPTION in the style even though the client area covers the whole
    // window (WM_NCCALCSIZE below). DWM only paints the Mica system backdrop
    // for windows that still have a frame, so stripping the caption entirely
    // makes it fall back to a flat neutral colour.
    LONG currentStyle = GetWindowLongW(hwnd_, GWL_STYLE);
    currentStyle |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    SetWindowLongW(hwnd_, GWL_STYLE, currentStyle);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    dpi_ = (float)GetDpiForWindow(hwnd_);
    if (dpi_ <= 0) dpi_ = 96.0f;
    canvas_.create(hwnd_, dpi_);
    centerInWorkArea();
    RECT client{};
    GetClientRect(hwnd_, &client);
    onResize((UINT)client.right, (UINT)client.bottom);
    applyTheme();

    ShowWindow(hwnd_, startHidden_ ? SW_HIDE : showCommand);
    UpdateWindow(hwnd_);
    applyTheme(); // re-apply once the window is visible so DWM samples the backdrop
    SetTimer(hwnd_, 1, 500, nullptr);

    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    addTrayIcon();

    startResolveWorker();
    if (!startupUrl_.empty()) {
        const std::wstring url = startupUrl_;
        startupUrl_.clear();
        ingestUrl(url);
    }

    aria_.start(util::ExeDir(), dataDir_, settings_.downloadDir, settings_.maxConcurrent,
                settings_.speedLimit,
                [this](const Aria2Snapshot& value) {
                    {
                        std::lock_guard<std::mutex> lock(snapshotMutex_);
                        snapshot_ = value;
                    }
                    if (!snapshotPosted_.exchange(true)) {
                        if (!PostMessageW(hwnd_, WM_APP_SNAPSHOT, 0, 0)) snapshotPosted_ = false;
                    }
                },
                [this](const std::wstring& message) {
                    {
                        std::lock_guard<std::mutex> lock(toastMutex_);
                        pendingToast_ = message;
                    }
                    if (!toastPosted_.exchange(true)) {
                        if (!PostMessageW(hwnd_, WM_APP_TOAST, 0, 0)) toastPosted_ = false;
                    }
                });
    return true;
}

int App::run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = reinterpret_cast<App*>(create->lpCreateParams);
        if (app) {
            app->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
    }
    if (app) return app->handleMessage(message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT App::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    // Explorer broadcasts this after a restart (or crash) and forgets every
    // notification icon, so put ours back.
    if (taskbarCreated_ != 0 && message == taskbarCreated_) {
        trayAdded_ = false;
        addTrayIcon();
        return 0;
    }

    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;

    case WM_NCHITTEST: {
        if (dialogOpen_) return HTCLIENT;
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &pt);
        float x = (float)pt.x * 96.0f / dpi_;
        float y = (float)pt.y * 96.0f / dpi_;
        if (width_ <= 0 || height_ <= 0) break;

        if (!IsZoomed(hwnd_)) {
            const float b = 5.0f;
            bool left = x < b, right = x > width_ - b, top = y < b, bottom = y > height_ - b;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }

        if (y < (float)kTitleHeight) {
            if (x >= width_ - 158.0f) return HTCLIENT;
            // The navigation pane toggle is a regular button, not drag space.
            if (x < 46.0f) return HTCLIENT;
            if (page_ != Page::Settings && searchRect_.contains(x, y)) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = ScaleInt(900, (UINT)dpi_);
        info->ptMinTrackSize.y = ScaleInt(620, (UINT)dpi_);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) {
            info->ptMaxPosition.x = monitor.rcWork.left - monitor.rcMonitor.left;
            info->ptMaxPosition.y = monitor.rcWork.top - monitor.rcMonitor.top;
            info->ptMaxSize.x = monitor.rcWork.right - monitor.rcWork.left;
            info->ptMaxSize.y = monitor.rcWork.bottom - monitor.rcWork.top;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        onPaint();
        return 0;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) onResize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_DPICHANGED: {
        UINT dpi = HIWORD(wParam);
        RECT* suggested = reinterpret_cast<RECT*>(lParam);
        onDpiChanged(dpi, suggested);
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet") == 0) {
            applyTheme();
        }
        return 0;

    case WM_TIMER:
        if (wParam == 2) {
            stepSidebarAnimation();
        } else if (wParam == 3) {
            // Drive the live progress bars while anything is downloading.
            sampleSpeeds();
            if (IsWindowVisible(hwnd_) && !IsIconic(hwnd_)) invalidate();
        } else {
            onTimer();
        }
        return 0;

    case WM_MOUSEMOVE:
        onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONDOWN:
        onMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONUP:
        onMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_LBUTTONDBLCLK:
        onMouseDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &pt);
        onMouseWheel(pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }

    case WM_MOUSELEAVE:
        hoverId_ = 0;
        hoverGid_.clear();
        hoverCursor_ = nullptr;
        trackingLeave_ = false;
        invalidate();
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(hoverCursor_ ? LoadCursorW(nullptr, hoverCursor_) : LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_KEYDOWN:
        onKeyDown(wParam);
        return 0;

    case WM_CHAR:
        onChar((wchar_t)wParam);
        return 0;

    case WM_APP_SNAPSHOT:
        onSnapshotMessage();
        return 0;

    case WM_APP_TOAST:
        onToastMessage();
        return 0;

    case WM_APP_INGEST:
        onIngestMessage();
        return 0;

    case WM_COPYDATA: {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (data && data->dwData == 0x4152444D && data->lpData && data->cbData >= sizeof(wchar_t)) {
            const size_t chars = data->cbData / sizeof(wchar_t);
            std::wstring url(static_cast<const wchar_t*>(data->lpData), chars);
            while (!url.empty() && url.back() == L'\0') url.pop_back();
            if (!url.empty()) ingestUrl(url);
            return TRUE;
        }
        break;
    }

    case WM_APP_SHOW:
        showFromTray();
        return 0;

    case WM_APP_TRAY: {
        // Legacy notification callbacks put the mouse message in lParam.
        UINT event = (UINT)lParam;
        if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK)
            showFromTray();
        else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU)
            showTrayMenu();
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kTrayMenuToggle:
            if (IsWindowVisible(hwnd_) && !IsIconic(hwnd_))
                hideToTray(false);
            else
                showFromTray();
            return 0;
        case kTrayMenuExit:
            requestExit();
            return 0;
        default:
            break;
        }
        break;

    case WM_CLOSE:
        // The title-bar close button and Alt+F4 only hide the window. The app
        // keeps running in the notification area until Exit is chosen there.
        if (!exiting_) {
            hideToTray();
            return 0;
        }
        removeTrayIcon();
        aria_.stop();
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        removeTrayIcon();
        KillTimer(hwnd_, 1);
        KillTimer(hwnd_, 2);
        KillTimer(hwnd_, 3);
        PostQuitMessage(0);
        return 0;

    case WM_ACTIVATE:
        invalidate();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void App::onPaint() {
    PAINTSTRUCT paint{};
    BeginPaint(hwnd_, &paint);
    if (!canvas_.ready()) canvas_.create(hwnd_, dpi_);
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (client.right > 0 && client.bottom > 0) {
        canvas_.resize((UINT)client.right, (UINT)client.bottom, dpi_);
        if (canvas_.begin()) {
            render();
            if (!canvas_.end()) InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    EndPaint(hwnd_, &paint);
}

void App::onResize(UINT pixelWidth, UINT pixelHeight) {
    width_ = (float)pixelWidth * 96.0f / dpi_;
    height_ = (float)pixelHeight * 96.0f / dpi_;
    canvas_.resize(pixelWidth, pixelHeight, dpi_);
    invalidate();
}

void App::centerInWorkArea() {
    if (!hwnd_) return;
    UINT dpi = (UINT)dpi_;
    if (dpi == 0) dpi = 96;
    HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return;

    const int workW = info.rcWork.right - info.rcWork.left;
    const int workH = info.rcWork.bottom - info.rcWork.top;
    int width = ScaleInt(1180, dpi);
    int height = ScaleInt(780, dpi);
    if (workW > 0) width = std::min(width, (int)(workW * 0.92f));
    if (workH > 0) height = std::min(height, (int)(workH * 0.92f));
    const int x = info.rcWork.left + (workW - width) / 2;
    const int y = info.rcWork.top + (workH - height) / 2;
    SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void App::onDpiChanged(UINT dpi, RECT* suggested) {
    dpi_ = dpi > 0 ? (float)dpi : 96.0f;
    if (suggested) {
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    onResize((UINT)client.right, (UINT)client.bottom);
    invalidate();
}

void App::onTimer() {
    if (focus_ != Focus::None || !toast_.empty()) invalidate();
}

void App::toggleSidebar() {
    sidebarCollapsed_ = !sidebarCollapsed_;
    settings_.sidebarCollapsed = sidebarCollapsed_;
    saveSettings();
    if (hwnd_) SetTimer(hwnd_, 2, 15, nullptr);
    sidebarAnimating_ = true;
    invalidate();
}

void App::stepSidebarAnimation() {
    const float target = sidebarCollapsed_ ? (float)kSidebarCompactWidth : (float)kSidebarWidth;
    const float delta = target - sidebarWidth_;
    if (std::fabs(delta) <= 1.0f) {
        sidebarWidth_ = target;
        sidebarAnimating_ = false;
        if (hwnd_) KillTimer(hwnd_, 2);
    } else {
        // Ease out: the pane moves quickly, then settles into place.
        sidebarWidth_ += delta * 0.32f;
    }
    invalidate();
}

void App::sampleSpeeds() {
    constexpr uint64_t kSampleMs = 200;
    constexpr size_t kMaxSamples = 256;
    const uint64_t now = util::NowMs();

    // Drop history for downloads that no longer exist (cleared completed).
    for (auto it = speedHistory_.begin(); it != speedHistory_.end();) {
        bool alive = false;
        for (const Aria2Item& item : uiSnapshot_.items) {
            if (item.gid == it->first) {
                alive = true;
                break;
            }
        }
        if (alive)
            ++it;
        else
            it = speedHistory_.erase(it);
    }

    for (const Aria2Item& item : uiSnapshot_.items) {
        if (item.isMetadataPlaceholder() || !item.isActive()) continue;
        SpeedHistory& h = speedHistory_[item.gid];
        if (h.lastSample == 0) {
            h.lastSample = now;
            h.samples.push_back((float)item.downSpeed);
            continue;
        }
        if (now - h.lastSample < kSampleMs) continue;
        h.lastSample = now;

        // Ease towards the reported rate so the silhouette glides instead of
        // stepping between aria2's 500 ms snapshots.
        float rate = (float)item.downSpeed;
        if (!h.samples.empty()) rate = h.samples.back() * 0.45f + rate * 0.55f;
        h.samples.push_back(rate);
        while (h.samples.size() > kMaxSamples) h.samples.pop_front();

        float maxRate = 65536.0f;
        for (float value : h.samples) maxRate = std::max(maxRate, value);
        h.peak = std::max(h.peak * 0.998f, maxRate);
        if (h.peak < 65536.0f) h.peak = 65536.0f;
    }
}

void App::updateItemTimes() {
    const std::time_t now = std::time(nullptr);

    for (auto it = itemTimes_.begin(); it != itemTimes_.end();) {
        bool alive = false;
        for (const Aria2Item& item : uiSnapshot_.items) {
            if (item.gid == it->first && !item.isMetadataPlaceholder()) {
                alive = true;
                break;
            }
        }
        if (alive)
            ++it;
        else
            it = itemTimes_.erase(it);
    }

    for (const Aria2Item& item : uiSnapshot_.items) {
        if (item.isMetadataPlaceholder()) continue;
        ItemTimes& times = itemTimes_[item.gid];

        WIN32_FILE_ATTRIBUTE_DATA data{};
        const bool haveFile =
            !item.path.empty() && GetFileAttributesExW(item.path.c_str(), GetFileExInfoStandard, &data);
        auto toTime = [](const FILETIME& stamp) -> std::time_t {
            ULARGE_INTEGER value;
            value.LowPart = stamp.dwLowDateTime;
            value.HighPart = stamp.dwHighDateTime;
            constexpr unsigned long long kEpoch = 116444736000000000ULL;
            if (value.QuadPart <= kEpoch) return 0;
            return (std::time_t)((value.QuadPart - kEpoch) / 10000000ULL);
        };

        if (times.completed == 0 && item.isComplete()) {
            times.completed = now;
            // The finished file's own timestamp is exact, so items restored
            // from the aria2 session show when they really finished.
            if (haveFile) {
                std::time_t stamp = toTime(data.ftLastWriteTime);
                if (stamp > 0) times.completed = stamp;
            }
        }
        if (times.added == 0) {
            // A .part file keeps the creation time of the original download,
            // which gives restored items a meaningful "added" time too.
            if (haveFile) {
                std::time_t stamp = toTime(data.ftCreationTime);
                if (stamp > 0) times.added = stamp;
            }
            if (times.added == 0) times.added = times.completed != 0 ? times.completed : now;
        }
    }
}

void App::onSnapshotMessage() {
    snapshotPosted_ = false;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        uiSnapshot_ = snapshot_;
    }
    updateItemTimes();
    sampleSpeeds();
    if (uiSnapshot_.state == EngineState::Connected) flushPendingAdds();
    bool animating = false;
    for (const Aria2Item& item : uiSnapshot_.items) {
        if (item.isActive()) {
            animating = true;
            break;
        }
    }
    if (hwnd_) {
        if (animating)
            SetTimer(hwnd_, 3, 33, nullptr);
        else
            KillTimer(hwnd_, 3);
    }
    invalidate();
}

void App::onToastMessage() {
    toastPosted_ = false;
    std::wstring message;
    {
        std::lock_guard<std::mutex> lock(toastMutex_);
        message = std::move(pendingToast_);
        pendingToast_.clear();
    }
    if (!message.empty()) showToast(message);
}

void App::applyTheme() {
    dark_ = util::IsDarkTheme();
    uint32_t accent = util::SystemAccentColor(dark_ ? 0xFF4CC2FFu : 0xFF0067C0u);
    if (hwnd_) {
        // The frameless client area covers the whole window, but WS_CAPTION is
        // still present (see WM_NCCALCSIZE) because DWM only paints the system
        // backdrop for windows it considers framed. Do not call
        // DwmExtendFrameIntoClientArea here: it disables the Mica sampling and
        // falls back to a flat neutral fill for DirectComposition windows.
        int backdrop = 2; // DWMSBT_MAINWINDOW (Mica)
        HRESULT backdropResult =
            DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
        if (FAILED(backdropResult)) {
            BOOL mica = TRUE;
            backdropResult = DwmSetWindowAttribute(hwnd_, DWMWA_MICA_EFFECT, &mica, sizeof(mica));
        }
        mica_ = SUCCEEDED(backdropResult);

        BOOL darkMode = dark_ ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
        int corner = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        COLORREF border = dark_ ? RGB(0x2A, 0x2A, 0x2C) : RGB(0xD4, 0xD4, 0xD8);
        DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    }
    colors_ = dark_ ? DarkColors(mica_, accent) : LightColors(mica_, accent);
    if (hwnd_) {
        AppLog(dataDir_, std::wstring(L"theme dark=") + (dark_ ? L"1" : L"0") + L" mica=" +
                             (mica_ ? L"1" : L"0") + L" composed=" +
                             (canvas_.composed() ? L"1" : L"0"));
    }
    invalidate();
}

void App::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::loadSettings() {
    if (dataDir_.empty()) {
        settings_.downloadDir = util::DownloadsDir();
        return;
    }
    if (settingsPath_.empty()) settingsPath_ = dataDir_ + L"\\settings.ini";
    std::wstring defaultDir = util::DownloadsDir();

    wchar_t buffer[2048]{};
    GetPrivateProfileStringW(L"aria2dm", L"downloadDir", defaultDir.c_str(), buffer,
                             (DWORD)std::size(buffer), settingsPath_.c_str());
    settings_.downloadDir = buffer[0] ? buffer : defaultDir;

    settings_.maxConcurrent = (int)GetPrivateProfileIntW(L"aria2dm", L"maxConcurrent", 5,
                                                        settingsPath_.c_str());
    if (settings_.maxConcurrent < 1) settings_.maxConcurrent = 1;
    if (settings_.maxConcurrent > 16) settings_.maxConcurrent = 16;

    wchar_t limitBuffer[64]{};
    GetPrivateProfileStringW(L"aria2dm", L"speedLimit", L"0", limitBuffer, (DWORD)std::size(limitBuffer),
                             settingsPath_.c_str());
    settings_.speedLimit = _wcstoi64(limitBuffer, nullptr, 10);
    if (settings_.speedLimit < 0) settings_.speedLimit = 0;

    settings_.startWithWindows =
        GetPrivateProfileIntW(L"aria2dm", L"startWithWindows", 1, settingsPath_.c_str()) != 0;

    settings_.sidebarCollapsed =
        GetPrivateProfileIntW(L"aria2dm", L"sidebarCollapsed", 0, settingsPath_.c_str()) != 0;

    if (settings_.downloadDir.empty()) settings_.downloadDir = defaultDir;
}

void App::saveSettings() {
    if (settingsPath_.empty()) return;
    WritePrivateProfileStringW(L"aria2dm", L"downloadDir", settings_.downloadDir.c_str(),
                               settingsPath_.c_str());
    WritePrivateProfileStringW(L"aria2dm", L"maxConcurrent", std::to_wstring(settings_.maxConcurrent).c_str(),
                               settingsPath_.c_str());
    WritePrivateProfileStringW(L"aria2dm", L"speedLimit", std::to_wstring(settings_.speedLimit).c_str(),
                               settingsPath_.c_str());
    WritePrivateProfileStringW(L"aria2dm", L"startWithWindows",
                               settings_.startWithWindows ? L"1" : L"0", settingsPath_.c_str());
    WritePrivateProfileStringW(L"aria2dm", L"sidebarCollapsed",
                               settings_.sidebarCollapsed ? L"1" : L"0", settingsPath_.c_str());
}

void App::applyStartupSetting() {
    util::SetRunAtStartup(L"Aria2DM", settings_.startWithWindows);
}

bool App::addTrayIcon() {
    NOTIFYICONDATAW nid = trayData();
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = (HICON)LoadImageW(instance_, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                  LR_SHARED);
    if (!nid.hIcon) nid.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
    wcscpy_s(nid.szTip, L"Aria2DM");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    AppLog(dataDir_, std::wstring(L"tray added=") + (trayAdded_ ? L"1" : L"0"));
    return trayAdded_;
}

void App::removeTrayIcon() {
    if (trayAdded_ && hwnd_) {
        NOTIFYICONDATAW nid = trayData();
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    trayAdded_ = false;
}

NOTIFYICONDATAW App::trayData() const {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    return nid;
}

void App::showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
    AppendMenuW(menu, MF_STRING, kTrayMenuToggle, visible ? L"Hide to tray" : L"Show Aria2DM");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayMenuExit, L"Exit Aria2DM");

    // The owner must be foreground for the menu to dismiss when it loses focus.
    SetForegroundWindow(hwnd_);
    POINT pt{};
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
}

void App::hideToTray(bool notify) {
    if (!hwnd_ || !IsWindowVisible(hwnd_)) return;
    ShowWindow(hwnd_, SW_HIDE);

    if (!notify || trayHintShown_ || !trayAdded_) return;
    trayHintShown_ = true;
    NOTIFYICONDATAW nid = trayData();
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, L"Aria2DM is still running");
    wcscpy_s(nid.szInfo,
             L"Downloads continue in the background. Use the tray icon to reopen or exit.");
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::showFromTray() {
    if (!hwnd_) return;
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    ShowWindow(hwnd_, SW_SHOW);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

    // Windows blocks SetForegroundWindow when the request reaches a background
    // process. Briefly sharing the input queue with the current foreground
    // thread lets a second-instance launch reliably raise this window.
    DWORD currentThread = GetCurrentThreadId();
    HWND foreground = GetForegroundWindow();
    DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    bool attached = foregroundThread != 0 && foregroundThread != currentThread &&
                    AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
    BringWindowToTop(hwnd_);
    SetForegroundWindow(hwnd_);
    SetActiveWindow(hwnd_);
    SetFocus(hwnd_);
    if (attached) AttachThreadInput(currentThread, foregroundThread, FALSE);

    invalidate();
}

void App::requestExit() {
    exiting_ = true;
    AppLog(dataDir_, L"exit requested");
    if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
}

void App::showToast(const std::wstring& text) {
    toast_ = text;
    toastUntil_ = util::NowMs() + 4200;
    invalidate();
}

