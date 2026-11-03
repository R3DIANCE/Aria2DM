#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>

#include "Aria2.h"

#include "Json.h"
#include "Util.h"

#include "resource.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring Quote(const std::wstring& s) {
    return L"\"" + s + L"\"";
}

int FindFreePort() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return 6800;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int result = 6800;
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
        int len = sizeof(addr);
        if (getsockname(s, (sockaddr*)&addr, &len) == 0) {
            int port = ntohs(addr.sin_port);
            if (port > 0) result = port;
        }
    }
    closesocket(s);
    return result;
}

std::wstring RandomHex(int byteCount) {
    std::random_device rd;
    static const wchar_t* digits = L"0123456789abcdef";
    std::wstring out;
    out.reserve((size_t)byteCount * 2);
    for (int i = 0; i < byteCount; ++i) {
        unsigned int v = rd() & 0xFF;
        out.push_back(digits[v >> 4]);
        out.push_back(digits[v & 0xF]);
    }
    return out;
}

std::wstring PercentDecode(const std::wstring& text) {
    std::string utf8 = util::Narrow(text);
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '%' && i + 2 < utf8.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(utf8[i + 1]), lo = hex(utf8[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(utf8[i] == '+' ? ' ' : utf8[i]);
    }
    return util::Widen(out);
}

std::wstring NameFromUri(const std::wstring& uri) {
    if (uri.empty()) return {};
    size_t q = uri.find(L'?');
    std::wstring clean = q == std::wstring::npos ? uri : uri.substr(0, q);
    size_t slash = clean.find_last_of(L"/\\");
    std::wstring name = slash == std::wstring::npos ? clean : clean.substr(slash + 1);
    return name;
}

std::wstring MagnetName(const std::wstring& uri) {
    size_t pos = uri.find(L"dn=");
    if (pos == std::wstring::npos) return {};
    pos += 3;
    size_t end = uri.find(L'&', pos);
    std::wstring value = uri.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
    return PercentDecode(value);
}

Aria2Item ParseItem(const Json& j) {
    Aria2Item it;
    it.gid = util::Widen(j["gid"].asString());
    it.status = util::Widen(j["status"].asString());
    it.total = j["totalLength"].asInt();
    it.completed = j["completedLength"].asInt();
    it.downSpeed = j["downloadSpeed"].asInt();
    it.upSpeed = j["uploadSpeed"].asInt();
    it.connections = (int)j["connections"].asInt();
    it.errorCode = (int)j["errorCode"].asInt();
    it.errorMessage = util::Widen(j["errorMessage"].asString());
    it.dir = util::Widen(j["dir"].asString());
    const Json& followedBy = j["followedBy"];
    if (followedBy.isArray() && followedBy.size() > 0) {
        it.followedBy = util::Widen(followedBy[0].asString());
    }

    const Json& files = j["files"];
    if (files.isArray()) {
        if (files.size() > 1) it.isTorrent = true;
        if (files.size() > 0) {
            const Json& first = files[0];
            it.path = util::Widen(first["path"].asString());
            const Json& uris = first["uris"];
            if (uris.isArray() && uris.size() > 0) {
                it.uri = util::Widen(uris[0]["uri"].asString());
            }
        }
    }

    const Json& bt = j["bittorrent"];
    if (bt.isObject()) {
        it.isTorrent = true;
        it.name = util::Widen(bt["info"]["name"].asString());
        it.numSeeders = (int)j["numSeeders"].asInt();
        it.seeder = j["seeder"].asString() == "true";
    }

    if (it.name.empty()) {
        if (!it.path.empty()) {
            it.name = util::FileName(it.path);
        } else if (it.uri.rfind(L"magnet:", 0) == 0) {
            it.name = MagnetName(it.uri);
        }
    }
    if (it.name.empty() && it.isTorrent) it.name = L"BitTorrent download";
    if (it.name.empty()) it.name = NameFromUri(it.uri);
    if (it.name.empty()) it.name = it.gid;
    if (it.uri.rfind(L"magnet:", 0) == 0) it.isTorrent = true;
    return it;
}

void DebugLog(const std::wstring& folder, const std::wstring& message) {
    if (folder.empty()) return;
    std::wstring path = folder + L"\\debug.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t stamp[64];
    swprintf_s(stamp, L"[%02d:%02d:%02d.%03d] ", now.wHour, now.wMinute, now.wSecond,
               now.wMilliseconds);
    std::wstring line = std::wstring(stamp) + message + L"\r\n";
    DWORD written = 0;
    WriteFile(file, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

bool ReadHttpResponse(HINTERNET request, std::string& response) {    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) return false;
        if (available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) return false;
        chunk.resize(read);
        response += chunk;
        if (response.size() > 32u * 1024u * 1024u) return false;
    }
    return true;
}

} // namespace

struct Aria2Client::Impl {
    SnapshotFn onSnapshot;
    NotifyFn onNotify;

    mutable std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> tasks;
    bool quit = false;
    bool retryRequested = false;
    std::thread worker;

    std::wstring exeDir;
    std::wstring dataDir;
    std::wstring downloadDir;
    std::wstring exePath;
    std::wstring sessionPath;
    std::wstring logPath;    int maxConcurrent = 5;
    long long speedLimit = 0;

    HANDLE process = nullptr;
    HANDLE job = nullptr;
    DWORD processId = 0;
    int port = 0;
    std::wstring secret;
    std::wstring version;
    bool rpcUp = false;
    HINTERNET http = nullptr;
    uint64_t lastPoll = 0;
    uint64_t nextStart = 0;
    Aria2Snapshot current;

    void run();
    bool launch();
    bool resolveEngine(std::wstring& outPath);
    bool waitReady();
    bool processAlive() const;
    void killProcess();
    void shutdownEngine();
    bool rpc(const std::wstring& method, Json params, Json& result, std::wstring& error);
    bool callList(const std::wstring& method, Json params, std::vector<Aria2Item>& out,
                  std::wstring& error);
    void poll();
    void publish();

    void notify(const std::wstring& message) {
        if (onNotify) onNotify(message);
    }

    bool quitting() {
        std::lock_guard<std::mutex> lock(mutex);
        return quit;
    }

    void enqueue(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.push_back(std::move(fn));
        }
        cv.notify_one();
    }

    void enqueueTask(std::function<void(Impl&)> fn) {
        enqueue([this, fn = std::move(fn)] { fn(*this); });
    }

    Json TokenParams() {
        Json params = Json::Array();
        params.push(Json(util::Narrow(L"token:" + secret)));
        return params;
    }

    Json Keys() {
        static const wchar_t* keys[] = { L"gid",           L"status",      L"totalLength",
                                         L"completedLength", L"downloadSpeed", L"uploadSpeed",
                                         L"connections",   L"errorCode",   L"errorMessage",
                                         L"files",         L"dir",         L"bittorrent",
                                         L"numSeeders",    L"seeder",      L"bitfield",
                                         L"followedBy" };
        Json arr = Json::Array();
        for (const wchar_t* key : keys) arr.push(Json(util::Narrow(key)));
        return arr;
    }
};

void Aria2Client::Impl::run() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    http = WinHttpOpen(L"Aria2DM/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                       WINHTTP_NO_PROXY_BYPASS, 0);
    if (http) WinHttpSetTimeouts(http, 1500, 1500, 3000, 4000);

    while (!quitting()) {
        bool retry = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (retryRequested) {
                retryRequested = false;
                retry = true;
            }
        }
        if (retry) {
            killProcess();
            rpcUp = false;
            nextStart = 0;
        }

        if (!rpcUp) {
            if (util::NowMs() >= nextStart) {
                current.state = EngineState::Starting;
                current.message = L"Starting the aria2 engine\u2026";
                publish();
                if (launch() && waitReady()) {
                    rpcUp = true;
                    current.state = EngineState::Connected;
                    current.message.clear();
                    current.version = version;
                    current.port = port;
                    lastPoll = 0;
                } else {
                    if (current.message.empty()) current.message = L"Could not start the aria2 engine";
                    current.state = EngineState::Error;
                    publish();
                    killProcess();
                    nextStart = util::NowMs() + 3000;
                }
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                tasks.clear();
            }
        } else {
            for (;;) {
                std::function<void()> task;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (tasks.empty()) break;
                    task = std::move(tasks.front());
                    tasks.pop_front();
                }
                task();
            }
            if (util::NowMs() - lastPoll >= 500) {
                lastPoll = util::NowMs();
                poll();
            }
        }

        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::milliseconds(120), [this] { return quit; });
    }

    shutdownEngine();
    if (job) {
        CloseHandle(job);
        job = nullptr;
    }
    if (http) {
        WinHttpCloseHandle(http);
        http = nullptr;
    }
    WSACleanup();
}

bool Aria2Client::Impl::processAlive() const {
    return process != nullptr && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

bool Aria2Client::Impl::resolveEngine(std::wstring& outPath) {
    // Development layout: an engine sitting next to the app wins.
    const std::wstring localCandidates[] = {exeDir + L"\\aria2\\aria2c.exe", exeDir + L"\\aria2c.exe"};
    for (const auto& candidate : localCandidates) {
        if (util::FileExists(candidate)) {
            outPath = candidate;
            return true;
        }
    }

    std::wstring engineDir = dataDir + L"\\engine";
    std::wstring target = engineDir + L"\\aria2c.exe";

    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_ARIA2_ENGINE), RT_RCDATA);
    if (!resource) {
        if (util::FileExists(target)) {
            outPath = target;
            return true;
        }
        DebugLog(dataDir, L"resolveEngine: engine resource missing");
        return false;
    }

    DWORD size = SizeofResource(nullptr, resource);
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data || size == 0) return false;

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (util::FileExists(target) &&
        GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &attributes) &&
        attributes.nFileSizeLow == size) {
        outPath = target;
        return true;
    }

    if (!util::EnsureDir(engineDir)) return false;

    std::wstring temp = target + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    if (!ok || written != size) {
        DeleteFileW(temp.c_str());
        return false;
    }

    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        // A previous engine may still be running from this path; reuse it.
        if (!util::FileExists(target)) return false;
    }

    DebugLog(dataDir, L"resolveEngine: unpacked " + std::to_wstring(size) + L" bytes to " + target);
    outPath = target;
    return true;
}

bool Aria2Client::Impl::launch() {
    killProcess();
    current.message.clear();

    exePath.clear();
    if (!resolveEngine(exePath)) {
        current.message = L"The built-in aria2 engine could not be unpacked.";
        DebugLog(dataDir, L"launch: no engine, dataDir=" + dataDir);
        return false;
    }
    DebugLog(dataDir, L"launch: engine=" + exePath);
    if (!util::EnsureDir(dataDir)) {
        current.message = L"Could not create the application data folder.";
        return false;
    }
    if (downloadDir.empty()) downloadDir = util::DownloadsDir();
    util::EnsureDir(downloadDir);

    sessionPath = dataDir + L"\\aria2.session";
    logPath = dataDir + L"\\aria2.log";
    port = FindFreePort();
    secret = RandomHex(16);

    std::wstring cmd;
    cmd += Quote(exePath);
    cmd += L" --enable-rpc=true --rpc-listen-all=false --rpc-listen-port=" + std::to_wstring(port);
    cmd += L" --rpc-secret=" + secret;
    cmd += L" --dir=" + Quote(downloadDir);
    cmd += L" --continue=true --file-allocation=none --auto-file-renaming=true";
    cmd += L" --max-concurrent-downloads=" + std::to_wstring(maxConcurrent);
    if (speedLimit > 0) cmd += L" --max-overall-download-limit=" + std::to_wstring(speedLimit);
    cmd += L" --save-session=" + Quote(sessionPath);
    cmd += L" --save-session-interval=30 --auto-save-interval=30";
    if (util::FileExists(sessionPath)) cmd += L" --input-file=" + Quote(sessionPath);
    cmd += L" --log=" + Quote(logPath) + L" --log-level=warn --summary-interval=0";
    cmd += L" --console-log-level=warn --user-agent=Aria2DM/1.0";
    // BitTorrent: a magnet link is only an info-hash, so the metadata has to be
    // fetched first and then the real torrent has to be followed. Without the
    // explicit metadata flags below aria2 can stop after writing the .torrent
    // file and never download the payload. Seed time is zero because this is a
    // download manager, not a seedbox.
    cmd += L" --seed-time=0 --follow-torrent=true";
    cmd += L" --bt-metadata-only=false --bt-save-metadata=false";
    cmd += L" --bt-max-peers=0 --bt-request-peer-speed-limit=0";
    cmd += L" --enable-dht=true --enable-dht6=false --dht-listen-port=6881-6999";
    cmd += L" --bt-enable-lpd=true --enable-peer-exchange=true";
    cmd += L" --bt-tracker-connect-timeout=10 --bt-tracker-timeout=10";
    cmd += L" --bt-stop-timeout=0 --bt-remove-unselected-file=false";
    // Many home networks advertise IPv6 without a working route. aria2 picks
    // the AAAA record first and fails with "unreachable network", so prefer
    // IPv4 for HTTP(S) downloads.
    cmd += L" --disable-ipv6=true";
    cmd += L" --connect-timeout=30 --timeout=60 --retry-wait=2 --max-tries=5";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    if (!CreateProcessW(exePath.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, dataDir.c_str(), &si, &pi)) {
        DWORD err = GetLastError();
        wchar_t buffer[128];
        swprintf_s(buffer, L"Could not launch aria2c.exe (error %lu).", err);
        current.message = buffer;
        DebugLog(dataDir, current.message);
        return false;
    }
    DebugLog(dataDir, L"launched aria2c pid=" + std::to_wstring(pi.dwProcessId) + L" port=" +
                           std::to_wstring(port));
    CloseHandle(pi.hThread);
    process = pi.hProcess;
    processId = pi.dwProcessId;

    // Make sure the engine never outlives the app, even if the app is killed.
    if (!job) {
        job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        }
    }
    if (job) AssignProcessToJobObject(job, process);

    current.state = EngineState::Starting;
    current.message = L"Starting the aria2 engine\u2026";
    current.port = port;
    publish();
    return true;
}

bool Aria2Client::Impl::waitReady() {
    for (int attempt = 0; attempt < 80; ++attempt) {
        if (quitting()) return false;
        if (!processAlive()) {
            current.message = L"aria2c.exe stopped during startup. See aria2.log in the app data folder.";
            DebugLog(dataDir, L"waitReady: process exited");
            return false;
        }
        Json result;
        std::wstring error;
        Json params = TokenParams();
        if (rpc(L"aria2.getVersion", std::move(params), result, error) && result.isObject()) {
            version = util::Widen(result["version"].asString());
            DebugLog(dataDir, L"waitReady: connected, version=" + version);
            return true;
        }
        DebugLog(dataDir, L"waitReady: attempt " + std::to_wstring(attempt) + L" failed: " + error);
        Sleep(100);
    }
    current.message = L"Timed out waiting for the aria2 engine.";
    DebugLog(dataDir, L"waitReady: timed out");
    return false;
}

void Aria2Client::Impl::killProcess() {
    if (process) {
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 1500);
        CloseHandle(process);
        process = nullptr;
        processId = 0;
    }
}

void Aria2Client::Impl::shutdownEngine() {
    if (rpcUp) {
        Json result;
        std::wstring error;
        Json saveParams = TokenParams();
        rpc(L"aria2.saveSession", std::move(saveParams), result, error);
        Json shutdownParams = TokenParams();
        rpc(L"aria2.shutdown", std::move(shutdownParams), result, error);
        if (process) WaitForSingleObject(process, 2500);
        rpcUp = false;
    }
    killProcess();
}

bool Aria2Client::Impl::rpc(const std::wstring& method, Json params, Json& result,
                            std::wstring& error) {
    error.clear();
    if (!http || port <= 0) {
        error = L"the engine is not running";
        return false;
    }

    Json request = Json::Object();
    request["jsonrpc"] = "2.0";
    request["id"] = "aria2dm";
    request["method"] = util::Narrow(method);
    request["params"] = std::move(params);
    std::string body = request.dump();

    HINTERNET connection =
        WinHttpConnect(http, L"127.0.0.1", (INTERNET_PORT)port, 0);
    if (!connection) {
        error = L"could not connect to the engine";
        return false;
    }
    HINTERNET handle = WinHttpOpenRequest(connection, L"POST", L"/jsonrpc", nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!handle) {
        WinHttpCloseHandle(connection);
        error = L"could not open an HTTP request";
        return false;
    }

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    BOOL ok = WinHttpSendRequest(handle, headers, (DWORD)-1L, body.data(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(handle, nullptr);

    std::string response;
    DWORD statusCode = 0;
    if (ok) {
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        ok = ReadHttpResponse(handle, response);
    }

    WinHttpCloseHandle(handle);
    WinHttpCloseHandle(connection);

    if (!ok) {
        error = L"the engine did not respond";
        DebugLog(dataDir, L"rpc " + method + L": no response");
        return false;
    }
    if (statusCode != 200) {
        wchar_t buffer[96];
        swprintf_s(buffer, L"HTTP status %lu", statusCode);
        error = buffer;
        DebugLog(dataDir, L"rpc " + method + L": HTTP " + std::to_wstring(statusCode) + L" req=" +
                               util::Widen(body) + L" resp=" + util::Widen(response));
        return false;
    }

    std::string parseError;
    Json reply = Json::Parse(response, &parseError);
    if (!parseError.empty()) {
        error = L"invalid response from the engine";
        DebugLog(dataDir, L"rpc " + method + L": parse error " + util::Widen(parseError) + L" body=" +
                               util::Widen(response.substr(0, 512)));
        return false;
    }
    if (reply.has("error") && reply["error"].isObject()) {
        error = util::Widen(reply["error"]["message"].asString());
        if (error.empty()) error = L"the engine reported an error";
        DebugLog(dataDir, L"rpc " + method + L": engine error " + error);
        return false;
    }
    if (reply.has("result")) {
        result = reply["result"];
        return true;
    }
    result = Json();
    return true;
}

bool Aria2Client::Impl::callList(const std::wstring& method, Json params,
                                 std::vector<Aria2Item>& out, std::wstring& error) {
    Json result;
    if (!rpc(method, std::move(params), result, error)) return false;
    if (!result.isArray()) return true;
    for (const Json& item : result.items()) {
        if (item.isObject()) out.push_back(ParseItem(item));
    }
    return true;
}

void Aria2Client::Impl::poll() {
    static const wchar_t* methods[] = { L"aria2.tellActive", L"aria2.tellWaiting", L"aria2.tellStopped" };
    std::vector<Aria2Item> lists[3];
    std::wstring error;

    {
        Json params = TokenParams();
        params.push(Keys());
        if (!callList(methods[0], std::move(params), lists[0], error)) goto lost;
    }
    {
        Json params = TokenParams();
        params.push(Json(0));
        params.push(Json(200));
        params.push(Keys());
        if (!callList(methods[1], std::move(params), lists[1], error)) goto lost;
    }
    {
        Json params = TokenParams();
        params.push(Json(0));
        params.push(Json(200));
        params.push(Keys());
        if (!callList(methods[2], std::move(params), lists[2], error)) goto lost;
    }

    {
        Json params = TokenParams();
        Json stat;
        if (!rpc(L"aria2.getGlobalStat", std::move(params), stat, error)) goto lost;

        Aria2Snapshot next = current;
        next.items.clear();
        next.items.reserve(lists[0].size() + lists[1].size() + lists[2].size());
        for (auto& item : lists[0]) next.items.push_back(std::move(item));
        for (auto& item : lists[1]) next.items.push_back(std::move(item));
        for (auto& item : lists[2]) next.items.push_back(std::move(item));
        next.downSpeed = stat["downloadSpeed"].asInt();
        next.upSpeed = stat["uploadSpeed"].asInt();
        next.active = (int)stat["numActive"].asInt();
        next.waiting = (int)stat["numWaiting"].asInt();
        next.stopped = (int)stat["numStopped"].asInt();
        next.state = EngineState::Connected;
        next.message.clear();
        next.version = version;
        next.port = port;
        current = std::move(next);
        publish();
    }
    return;

lost:
    rpcUp = false;
    current.state = EngineState::Error;
    current.message = L"Lost connection to the aria2 engine. Reconnecting\u2026";
    publish();
    killProcess();
    nextStart = util::NowMs() + 1500;
}

void Aria2Client::Impl::publish() {
    if (onSnapshot) onSnapshot(current);
}

// ---------------------------------------------------------------------------

Aria2Client::Aria2Client() : impl_(std::make_unique<Impl>()) {}

Aria2Client::~Aria2Client() { stop(); }

void Aria2Client::start(std::wstring exeDir, std::wstring dataDir, std::wstring downloadDir,
                        int maxConcurrent, long long speedLimit, SnapshotFn onSnapshot,
                        NotifyFn onNotify) {
    stop();
    impl_->exeDir = std::move(exeDir);
    impl_->dataDir = std::move(dataDir);
    impl_->downloadDir = std::move(downloadDir);
    impl_->maxConcurrent = maxConcurrent < 1 ? 1 : maxConcurrent;
    impl_->speedLimit = speedLimit;
    impl_->onSnapshot = std::move(onSnapshot);
    impl_->onNotify = std::move(onNotify);
    impl_->quit = false;
    impl_->retryRequested = false;
    impl_->rpcUp = false;
    impl_->lastPoll = 0;
    impl_->nextStart = 0;
    impl_->current = Aria2Snapshot{};
    impl_->worker = std::thread([this] { impl_->run(); });
}

void Aria2Client::stop() {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->quit = true;
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->onSnapshot = nullptr;
    impl_->onNotify = nullptr;
}

bool Aria2Client::running() const {
    return impl_ && impl_->worker.joinable();
}

void Aria2Client::addUri(std::wstring url, std::wstring dir, std::wstring out,
                         bool singleConnection) {
    impl_->enqueueTask([url, dir, out, singleConnection](Impl& self) {
        Json options = Json::Object();
        if (!dir.empty()) options["dir"] = util::Narrow(dir);
        bool magnet = url.rfind(L"magnet:", 0) == 0;
        if (!out.empty() && !magnet) options["out"] = util::Narrow(out);
        if (singleConnection) {
            // The local MEGA proxy decrypts one stream and keeps a single
            // upstream connection; letting aria2 open many would multiply the
            // upstream requests and waste memory.
            options["split"] = "1";
            options["max-connection-per-server"] = "1";
            options["no-proxy"] = "127.0.0.1,localhost";
        }
        if (magnet) {
            // Make the magnet resolve to the real torrent payload. Without
            // these the engine can stop once the info-dictionary is saved.
            options["bt-metadata-only"] = "false";
            options["bt-save-metadata"] = "false";
            options["follow-torrent"] = "true";
            options["seed-time"] = "0";
        }
        Json params = self.TokenParams();
        Json uris = Json::Array();
        uris.push(Json(util::Narrow(url)));
        params.push(uris);
        params.push(options);
        Json result;
        std::wstring error;
        if (!self.rpc(L"aria2.addUri", std::move(params), result, error)) {
            self.notify(L"Could not add download: " + error);
        } else {
            self.lastPoll = 0;
        }
    });
}

void Aria2Client::pauseItem(std::wstring gid, bool pause) {
    impl_->enqueueTask([gid, pause](Impl& self) {
        Json params = self.TokenParams();
        params.push(Json(util::Narrow(gid)));
        Json result;
        std::wstring error;
        if (!self.rpc(pause ? L"aria2.pause" : L"aria2.unpause", std::move(params), result, error)) {
            self.notify((pause ? L"Could not pause download: " : L"Could not resume download: ") + error);
        } else {
            self.lastPoll = 0;
        }
    });
}

void Aria2Client::removeItem(std::wstring gid, std::wstring status) {
    impl_->enqueueTask([gid, status](Impl& self) {
        bool running = status == L"active" || status == L"waiting" || status == L"paused";
        if (running) {
            Json params = self.TokenParams();
            params.push(Json(util::Narrow(gid)));
            Json result;
            std::wstring error;
            if (!self.rpc(L"aria2.forceRemove", std::move(params), result, error)) {
                self.notify(L"Could not remove download: " + error);
                return;
            }
            Sleep(180);
        }

        Json params = self.TokenParams();
        params.push(Json(util::Narrow(gid)));
        Json result;
        std::wstring error;
        if (!self.rpc(L"aria2.removeDownloadResult", std::move(params), result, error) && !running) {
            self.notify(L"Could not remove download: " + error);
        }
        self.lastPoll = 0;
    });
}

void Aria2Client::retryItem(std::wstring gid, std::wstring status, std::wstring uri,
                            std::wstring dir, std::wstring name, bool torrent) {
    impl_->enqueueTask([gid, status, uri, dir, name, torrent](Impl& self) {
        if (uri.empty()) {
            self.notify(L"No source link is available for this download.");
            return;
        }
        {
            Json params = self.TokenParams();
            params.push(Json(util::Narrow(gid)));
            Json result;
            std::wstring error;
            self.rpc(L"aria2.removeDownloadResult", std::move(params), result, error);
        }

        Json options = Json::Object();
        if (!dir.empty()) options["dir"] = util::Narrow(dir);
        // Forcing an output name would collapse a multi-file torrent into one
        // file, so only file downloads get an explicit name.
        if (!name.empty() && !torrent) options["out"] = util::Narrow(name);
        if (torrent) {
            options["bt-metadata-only"] = "false";
            options["bt-save-metadata"] = "false";
            options["follow-torrent"] = "true";
            options["seed-time"] = "0";
        }
        Json params = self.TokenParams();
        Json uris = Json::Array();
        uris.push(Json(util::Narrow(uri)));
        params.push(uris);
        params.push(options);
        Json result;
        std::wstring error;
        if (!self.rpc(L"aria2.addUri", std::move(params), result, error)) {
            self.notify(L"Could not restart download: " + error);
        } else {
            self.lastPoll = 0;
        }
    });
}

void Aria2Client::clearCompleted() {
    impl_->enqueueTask([](Impl& self) {
        Json params = self.TokenParams();
        params.push(Json(0));
        params.push(Json(200));
        params.push(self.Keys());
        Json result;
        std::wstring error;
        if (!self.rpc(L"aria2.tellStopped", std::move(params), result, error)) {
            self.notify(L"Could not clear completed downloads: " + error);
            return;
        }
        if (!result.isArray()) return;
        for (const Json& item : result.items()) {
            Json remove = self.TokenParams();
            remove.push(Json(item["gid"].asString()));
            Json ignored;
            std::wstring ignoredError;
            self.rpc(L"aria2.removeDownloadResult", std::move(remove), ignored, ignoredError);
        }
        self.lastPoll = 0;
    });
}

void Aria2Client::applyOptions(int maxConcurrent, long long speedLimit, std::wstring dir) {
    maxConcurrent = maxConcurrent < 1 ? 1 : maxConcurrent;
    impl_->maxConcurrent = maxConcurrent;
    impl_->speedLimit = speedLimit;
    if (!dir.empty()) impl_->downloadDir = dir;
    impl_->enqueueTask([maxConcurrent, speedLimit, dir](Impl& self) {
        Json options = Json::Object();
        options["max-concurrent-downloads"] = std::to_string(maxConcurrent);
        options["max-overall-download-limit"] = std::to_string(speedLimit);
        if (!dir.empty()) options["dir"] = util::Narrow(dir);
        Json params = self.TokenParams();
        params.push(options);
        Json result;
        std::wstring error;
        if (!self.rpc(L"aria2.changeGlobalOption", std::move(params), result, error)) {
            self.notify(L"Could not apply settings: " + error);
        } else {
            self.lastPoll = 0;
        }
        self.maxConcurrent = maxConcurrent;
        self.speedLimit = speedLimit;
    });
}

void Aria2Client::retryEngine() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->retryRequested = true;
    }
    impl_->cv.notify_one();
}
