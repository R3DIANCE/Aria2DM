#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <cstdint>
#include <ctime>

namespace util {

inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

inline std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

inline std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

inline bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    return Lower(haystack).find(Lower(needle)) != std::wstring::npos;
}

inline std::wstring FormatBytes(long long bytes) {
    if (bytes < 0) bytes = 0;
    static const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    wchar_t buf[64];
    if (u == 0)
        swprintf_s(buf, L"%lld B", bytes);
    else
        swprintf_s(buf, L"%.1f %s", v, units[u]);
    return buf;
}

inline std::wstring FormatSpeed(long long bytesPerSec) {
    if (bytesPerSec <= 0) return L"0 B/s";
    return FormatBytes(bytesPerSec) + L"/s";
}

inline std::wstring FormatEta(long long seconds) {
    if (seconds < 0) return L"";
    if (seconds < 60) {
        wchar_t buf[32];
        swprintf_s(buf, L"%llds", seconds);
        return buf;
    }
    if (seconds < 3600) {
        wchar_t buf[32];
        swprintf_s(buf, L"%lldm %02llds", seconds / 60, seconds % 60);
        return buf;
    }
    wchar_t buf[32];
    swprintf_s(buf, L"%lldh %02lldm", seconds / 3600, (seconds % 3600) / 60);
    return buf;
}

// Short wall-clock label for the "Added" / "Completed" table columns. Recent
// days get a relative prefix, older dates show the month and, for previous
// years, the year too.
inline long long DaysFromCivil(int year, int month, int day) {
    year -= month <= 2;
    const long long era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy =
        (153u * (unsigned)(month + (month > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + (long long)doe - 719468LL;
}

inline std::wstring FormatTimeShort(std::time_t value) {
    if (value <= 0) return L"\u2014";
    std::tm tm{};
    if (localtime_s(&tm, &value) != 0) return L"\u2014";
    const std::time_t now = std::time(nullptr);
    std::tm today{};
    localtime_s(&today, &now);

    static const wchar_t* months[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                      L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    const long long diff = DaysFromCivil(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday) -
                           DaysFromCivil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

    wchar_t buf[64];
    if (diff == 0)
        swprintf_s(buf, L"Today %02d:%02d", tm.tm_hour, tm.tm_min);
    else if (diff == 1)
        swprintf_s(buf, L"Yest. %02d:%02d", tm.tm_hour, tm.tm_min);
    else if (tm.tm_year == today.tm_year)
        swprintf_s(buf, L"%s %d %02d:%02d", months[tm.tm_mon], tm.tm_mday, tm.tm_hour, tm.tm_min);
    else
        swprintf_s(buf, L"%s %d, %d", months[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900);
    return buf;
}

inline std::wstring FileName(const std::wstring& path) {
    if (path.empty()) return {};
    std::filesystem::path p(path);
    std::wstring name = p.filename().wstring();
    if (name.empty()) name = p.parent_path().filename().wstring();
    return name;
}

inline std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    if (n == 0) return {};
    return std::wstring(buf, n);
}

inline std::wstring ExeDir() {
    std::filesystem::path p(ExePath());
    return p.parent_path().wstring();
}

inline std::wstring LocalAppData() {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
        out = p;
        CoTaskMemFree(p);
    }
    if (out.empty()) {
        wchar_t buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) out.assign(buf, n);
    }
    return out;
}

inline std::wstring DownloadsDir() {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) {
        out = p;
        CoTaskMemFree(p);
    }
    if (out.empty()) {
        wchar_t buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) out = std::wstring(buf, n) + L"\\Downloads";
    }
    return out;
}

inline bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool DirExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool EnsureDir(const std::wstring& path) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path), ec);
    return DirExists(path);
}

inline uint64_t NowMs() {
    return GetTickCount64();
}

inline bool IsDarkTheme() {
    HKEY key = nullptr;
    bool dark = true;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD value = 1, size = sizeof(value), type = 0;
        if (RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type, (BYTE*)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD) {
            dark = value == 0;
        }
        RegCloseKey(key);
    }
    return dark;
}

// The accent colour chosen in Windows Settings, as 0xFFRRGGBB.
inline uint32_t SystemAccentColor(uint32_t fallback) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return fallback;
    }
    DWORD value = 0, size = sizeof(value), type = 0;
    uint32_t result = fallback;
    if (RegQueryValueExW(key, L"AccentColor", nullptr, &type, (BYTE*)&value, &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        // Stored as 0xAABBGGRR.
        uint32_t r = value & 0xFF;
        uint32_t g = (value >> 8) & 0xFF;
        uint32_t b = (value >> 16) & 0xFF;
        result = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    RegCloseKey(key);
    return result;
}

inline void RevealInExplorer(const std::wstring& path) {    if (path.empty()) return;
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        CoTaskMemFree(pidl);
    } else {
        std::wstring dir = DirExists(path) ? path : std::filesystem::path(path).parent_path().wstring();
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

inline void OpenFolder(const std::wstring& path) {
    if (path.empty()) return;
    EnsureDir(path);
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Writes or removes the per-user Run entry used by "Start with Windows".
// The command is quoted so paths containing spaces still launch correctly.
inline void SetRunAtStartup(const std::wstring& valueName, bool enabled) {
    static constexpr wchar_t kRunKey[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key = nullptr;
    if (enabled) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                            &key, nullptr) != ERROR_SUCCESS) {
            return;
        }
        std::wstring command = L"\"" + ExePath() + L"\"";
        RegSetValueExW(key, valueName.c_str(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(command.c_str()),
                       (DWORD)((command.size() + 1) * sizeof(wchar_t)));
    } else {
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
            return;
        RegDeleteValueW(key, valueName.c_str());
    }
    RegCloseKey(key);
}

inline std::wstring ReadAllText(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    std::string data;
    if (size.QuadPart > 0 && size.QuadPart < 1024 * 1024) {
        data.resize((size_t)size.QuadPart);
        DWORD read = 0;
        ReadFile(h, data.data(), (DWORD)data.size(), &read, nullptr);
        data.resize(read);
    }
    CloseHandle(h);
    return Widen(data);
}

} // namespace util
