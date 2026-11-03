#include "BrowserHost.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "App.h"
#include "Json.h"
#include "resource.h"
#include "Util.h"

namespace {

constexpr ULONG_PTR kCopyDataMagic = 0x4152444D; // 'ARMD'
constexpr wchar_t kHostName[] = L"com.aria2dm.host";
constexpr wchar_t kExtensionId[] = L"peinfbndlcgigijkbiecikjhjggaalff";
constexpr wchar_t kFirefoxId[] = L"aria2dm@local";

std::string JsonQuotePath(std::wstring path) {
    std::string out;
    out.reserve(path.size() + 8);
    for (wchar_t ch : path) {
        if (ch == L'\\') {
            out += "\\\\";
        } else if (ch == L'"') {
            out += "\\\"";
        } else if (ch < 128) {
            out.push_back((char)ch);
        } else {
            out += util::Narrow(std::wstring(1, ch));
        }
    }
    return out;
}

bool WriteText(const std::wstring& path, const std::string& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = text.empty() || WriteFile(file, text.data(), (DWORD)text.size(), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && (text.empty() || written == text.size());
}

bool WriteResource(int id, const std::wstring& path) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) return false;
    const DWORD size = SizeofResource(nullptr, resource);
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data || size == 0) return false;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == size;
}

bool RegisterHost(const wchar_t* keyPath, const std::wstring& manifestPath) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = (DWORD)((manifestPath.size() + 1) * sizeof(wchar_t));
    const LONG result = RegSetValueExW(key, nullptr, 0, REG_SZ,
                                       reinterpret_cast<const BYTE*>(manifestPath.c_str()), bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool ForwardToWindow(const std::wstring& url) {
    HWND hwnd = FindWindowW(kAppWindowClass, nullptr);
    if (!hwnd) return false;

    COPYDATASTRUCT data{};
    data.dwData = kCopyDataMagic;
    data.cbData = (DWORD)((url.size() + 1) * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(url.c_str());

    DWORD_PTR result = 0;
    const LRESULT sent = SendMessageTimeoutW(hwnd, WM_COPYDATA, 0,
                                             reinterpret_cast<LPARAM>(&data),
                                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &result);
    return sent != 0;
}

bool LaunchWithUrl(const std::wstring& url) {
    const std::wstring exe = util::ExePath();
    if (exe.empty()) return false;
    // Raw quotes cannot appear in a URL, but strip them defensively so the
    // command line cannot be broken by a malformed link.
    std::wstring safe;
    safe.reserve(url.size());
    for (wchar_t ch : url) {
        if (ch != L'"') safe.push_back(ch);
    }
    std::wstring command = L"\"" + exe + L"\" --background --add \"" + safe + L"\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const std::wstring dir = util::ExeDir();
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, dir.c_str(), &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool ForwardToApp(const std::wstring& url, std::wstring& error) {
    if (url.empty() || (url.rfind(L"http://", 0) != 0 && url.rfind(L"https://", 0) != 0)) {
        error = L"Only http and https links are supported.";
        return false;
    }
    if (ForwardToWindow(url)) return true;
    // The desktop app is not running yet: start it hidden with the link.
    if (LaunchWithUrl(url)) return true;
    error = L"Aria2DM is not running and could not be started.";
    return false;
}

bool ReadExact(HANDLE handle, void* data, DWORD length) {
    DWORD total = 0;
    while (total < length) {
        DWORD read = 0;
        if (!ReadFile(handle, static_cast<char*>(data) + total, length - total, &read, nullptr) ||
            read == 0) {
            return false;
        }
        total += read;
    }
    return true;
}

bool WriteExact(HANDLE handle, const void* data, DWORD length) {
    DWORD total = 0;
    while (total < length) {
        DWORD written = 0;
        if (!WriteFile(handle, static_cast<const char*>(data) + total, length - total, &written,
                       nullptr) ||
            written == 0) {
            return false;
        }
        total += written;
    }
    return true;
}

} // namespace

bool RunNativeMessagingHost() {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) return false;

    for (;;) {
        uint32_t length = 0;
        if (!ReadExact(input, &length, sizeof(length))) break;
        if (length == 0 || length > 4 * 1024 * 1024) break;

        std::string text(length, '\0');
        if (!ReadExact(input, text.data(), length)) break;

        const Json request = Json::Parse(text);
        const std::string type = request["type"].asString();
        Json reply = Json::Object();
        if (type == "ping") {
            reply["ok"] = true;
            reply["version"] = "0.1";
        } else if (type == "download") {
            const std::wstring url = util::Widen(request["url"].asString());
            std::wstring error;
            if (ForwardToApp(url, error)) {
                reply["ok"] = true;
            } else {
                reply["ok"] = false;
                reply["error"] = util::Narrow(error);
            }
        } else {
            reply["ok"] = false;
            reply["error"] = "unknown message type";
        }

        const std::string answer = reply.dump();
        const uint32_t answerLength = (uint32_t)answer.size();
        if (!WriteExact(output, &answerLength, sizeof(answerLength)) ||
            !WriteExact(output, answer.data(), answerLength)) {
            break;
        }
        FlushFileBuffers(output);
    }
    return true;
}

bool InstallBrowserIntegration(const std::wstring& dataDir, std::wstring& message) {
    if (dataDir.empty()) {
        message = L"The application data folder is unavailable.";
        return false;
    }

    const std::wstring root = dataDir + L"\\browser";
    const std::wstring chromium = root + L"\\chromium";
    const std::wstring firefox = root + L"\\firefox";
    if (!util::EnsureDir(chromium) || !util::EnsureDir(firefox)) {
        message = L"Could not create the browser extension folder.";
        return false;
    }

    bool ok = true;
    ok = WriteResource(IDR_EXT_CHROME_MANIFEST, chromium + L"\\manifest.json") && ok;
    ok = WriteResource(IDR_EXT_CHROME_BACKGROUND, chromium + L"\\background.js") && ok;
    ok = WriteResource(IDR_EXT_CHROME_CONTENT, chromium + L"\\content.js") && ok;
    ok = WriteResource(IDR_EXT_FIREFOX_MANIFEST, firefox + L"\\manifest.json") && ok;
    ok = WriteResource(IDR_EXT_FIREFOX_BACKGROUND, firefox + L"\\background.js") && ok;
    ok = WriteResource(IDR_EXT_FIREFOX_CONTENT, firefox + L"\\content.js") && ok;
    ok = WriteResource(IDR_EXT_README, root + L"\\README.txt") && ok;

    const std::wstring exe = util::ExePath();
    const std::wstring chromiumManifest = root + L"\\chromium-host.json";
    const std::wstring firefoxManifest = root + L"\\firefox-host.json";

    std::string chromiumJson = "{\n  \"name\": \"" + util::Narrow(kHostName) + "\",\n";
    chromiumJson += "  \"description\": \"Aria2DM browser bridge\",\n";
    chromiumJson += "  \"path\": \"" + JsonQuotePath(exe) + "\",\n";
    chromiumJson += "  \"type\": \"stdio\",\n";
    chromiumJson += "  \"allowed_origins\": [\"chrome-extension://" + util::Narrow(kExtensionId) +
                    "/\"]\n}\n";
    ok = WriteText(chromiumManifest, chromiumJson) && ok;

    std::string firefoxJson = "{\n  \"name\": \"" + util::Narrow(kHostName) + "\",\n";
    firefoxJson += "  \"description\": \"Aria2DM browser bridge\",\n";
    firefoxJson += "  \"path\": \"" + JsonQuotePath(exe) + "\",\n";
    firefoxJson += "  \"type\": \"stdio\",\n";
    firefoxJson += "  \"allowed_extensions\": [\"" + util::Narrow(kFirefoxId) + "\"]\n}\n";
    ok = WriteText(firefoxManifest, firefoxJson) && ok;

    const wchar_t* chromiumKeys[] = {
        L"Software\\Google\\Chrome\\NativeMessagingHosts\\com.aria2dm.host",
        L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.aria2dm.host",
        L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\com.aria2dm.host",
        L"Software\\Vivaldi\\NativeMessagingHosts\\com.aria2dm.host",
        L"Software\\Chromium\\NativeMessagingHosts\\com.aria2dm.host",
        L"Software\\Opera Software\\NativeMessagingHosts\\com.aria2dm.host",
    };
    for (const wchar_t* key : chromiumKeys) RegisterHost(key, chromiumManifest);
    RegisterHost(L"Software\\Mozilla\\NativeMessagingHosts\\com.aria2dm.host", firefoxManifest);

    if (!ok) {
        message = L"Some browser integration files could not be written.";
        return false;
    }
    message = L"Browser bridge installed. Load the extension from " + root;
    return true;
}
