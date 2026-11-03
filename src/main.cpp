#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include "App.h"
#include "BrowserHost.h"
#include "Providers.h"
#include "UiIds.h"
#include "Util.h"

namespace {

constexpr ULONG_PTR kCopyDataMagic = 0x4152444D; // 'ARMD'

HANDLE g_instanceMutex = nullptr;

struct StartupOptions {
    std::wstring addUrl;
    std::wstring resolveUrl;
    bool background = false;
    bool nativeHost = false;
    bool installBrowser = false;
};

StartupOptions ParseCommandLine() {
    StartupOptions options;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return options;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--add" && i + 1 < argc) {
            options.addUrl = argv[++i];
        } else if (arg.rfind(L"--add=", 0) == 0) {
            options.addUrl = arg.substr(6);
        } else if (arg == L"--resolve" && i + 1 < argc) {
            options.resolveUrl = argv[++i];
        } else if (arg == L"--background") {
            options.background = true;
        } else if (arg == L"--native-host") {
            options.nativeHost = true;
        } else if (arg == L"--install-browser") {
            options.installBrowser = true;
        } else if (arg.rfind(L"chrome-extension://", 0) == 0 ||
                   arg.rfind(L"moz-extension://", 0) == 0) {
            // Chrome and Firefox pass the calling extension as the first
            // argument when they launch a native messaging host.
            options.nativeHost = true;
        }
    }
    LocalFree(argv);
    return options;
}

// Shows and raises the already-running copy. Attaching to the foreground
// thread briefly makes this work even when the second process was started from
// a launcher that does not own the foreground.
void ActivateExistingInstance(HWND existing) {
    ShowWindowAsync(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);

    DWORD currentThread = GetCurrentThreadId();
    HWND foreground = GetForegroundWindow();
    DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    bool attached = foregroundThread != 0 && foregroundThread != currentThread &&
                    AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
    BringWindowToTop(existing);
    SetForegroundWindow(existing);
    SetActiveWindow(existing);
    SetFocus(existing);
    if (attached) AttachThreadInput(currentThread, foregroundThread, FALSE);

    PostMessageW(existing, WM_APP_SHOW, 0, 0);
}

bool SendUrlToWindow(HWND existing, const std::wstring& url) {
    COPYDATASTRUCT data{};
    data.dwData = kCopyDataMagic;
    data.cbData = (DWORD)((url.size() + 1) * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(url.c_str());
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &result) != 0;
}

// Creates the single-instance mutex. If another copy already owns it, hand the
// new link over (or just present the running window) and return false.
bool ClaimSingleInstance(const StartupOptions& options) {
    g_instanceMutex = CreateMutexW(nullptr, FALSE, kAppInstanceMutex);
    if (!g_instanceMutex) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return true;

    // The process the user just launched still holds foreground rights, so
    // pass them on to the running copy before asking it to show itself.
    AllowSetForegroundWindow(ASFW_ANY);
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (HWND existing = FindWindowW(kAppWindowClass, nullptr)) {
            if (!options.addUrl.empty()) {
                // Browser downloads should not steal focus from the page.
                SendUrlToWindow(existing, options.addUrl);
            } else {
                ActivateExistingInstance(existing);
            }
            return false;
        }
        Sleep(50);
    }
    return false;
}

void ReleaseSingleInstance() {
    if (g_instanceMutex) {
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const StartupOptions options = ParseCommandLine();

    // Native messaging hosts are console-style helpers launched by the
    // browser. They must never create a window or claim the single-instance
    // mutex; they only forward one message and exit.
    if (options.nativeHost) return RunNativeMessagingHost() ? 0 : 1;

    if (!options.resolveUrl.empty()) {
        const ResolvedLink link = ResolveLink(options.resolveUrl);
        wchar_t summary[2048];
        swprintf_s(summary,
                   L"provider=%d\nurl=%ls\nname=%ls\nsize=%lld\nneedsKey=%d\nerror=%ls\n",
                   (int)link.provider, link.url.c_str(), link.filename.c_str(), link.size,
                   link.needsKey ? 1 : 0, link.error.c_str());
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out && out != INVALID_HANDLE_VALUE) {
            std::string utf8 = util::Narrow(summary);
            DWORD written = 0;
            WriteFile(out, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        }
        const std::wstring file = util::LocalAppData() + L"\\Aria2DM\\resolve.txt";
        util::EnsureDir(util::LocalAppData() + L"\\Aria2DM");
        HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            const std::string utf8 = util::Narrow(summary);
            DWORD written = 0;
            WriteFile(handle, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
            CloseHandle(handle);
        }
        return 0;
    }

    if (options.installBrowser) {
        std::wstring message;
        const bool ok =
            InstallBrowserIntegration(util::LocalAppData() + L"\\Aria2DM", message);
        MessageBoxW(nullptr, message.c_str(), L"Aria2DM",
                    ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
        return ok ? 0 : 1;
    }

    if (!ClaimSingleInstance(options)) {
        ReleaseSingleInstance();
        return 0;
    }

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com)) {
        ReleaseSingleInstance();
        return 1;
    }

    int result = 1;
    {
        App app(instance);
        app.setStartHidden(options.background);
        app.queueStartupUrl(options.addUrl);
        if (app.create(options.background ? SW_HIDE : showCommand)) result = app.run();
    }

    CoUninitialize();
    ReleaseSingleInstance();
    return result;
}
