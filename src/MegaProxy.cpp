#include "MegaProxy.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "Providers.h"
#include "Util.h"

namespace {

struct ProxyFile {
    std::wstring remote;
    std::vector<unsigned char> key;
    long long size = 0;
    std::wstring name;
};

struct ProxyState {
    std::atomic<bool> running{false};
    SOCKET listener = INVALID_SOCKET;
    bool wsa = false;
    int port = 0;

    std::mutex mutex;
    std::map<std::string, ProxyFile> files;
    std::set<SOCKET> clients;
    int nextId = 1;

    std::wstring addFile(const std::wstring& remote, const std::vector<unsigned char>& key,
                         long long size, const std::wstring& name) {
        std::lock_guard<std::mutex> lock(mutex);
        char token[64];
        std::snprintf(token, sizeof(token), "%d%08x", nextId++, (unsigned)GetTickCount());
        files[token] = ProxyFile{remote, key, size, name};
        wchar_t url[128];
        swprintf_s(url, L"http://127.0.0.1:%d/file/%hs", port, token);
        return url;
    }

    std::string headerValue(const std::string& raw, const std::string& name) {
        std::string lowered = raw;
        std::string key = name;
        for (char& c : lowered) c = (char)tolower((unsigned char)c);
        for (char& c : key) c = (char)tolower((unsigned char)c);
        size_t pos = 0;
        while ((pos = lowered.find("\r\n" + key + ":", pos)) != std::string::npos) {
            pos += key.size() + 3;
            size_t end = raw.find("\r\n", pos);
            std::string value = raw.substr(pos, end == std::string::npos ? std::string::npos
                                                                         : end - pos);
            size_t first = value.find_first_not_of(" \t");
            if (first == std::string::npos) return {};
            value.erase(0, first);
            size_t last = value.find_last_not_of(" \t\r");
            if (last != std::string::npos) value.resize(last + 1);
            return value;
        }
        return {};
    }

    static std::wstring AsciiFilename(const std::wstring& name) {
        std::wstring out;
        for (wchar_t ch : name) {
            if (ch >= 32 && ch < 127 && ch != L'"' && ch != L'\\')
                out.push_back(ch);
            else
                out.push_back(L'_');
        }
        if (out.empty()) out = L"mega-download.bin";
        return out;
    }

    static bool SendAll(SOCKET socket, const char* data, size_t length) {
        while (length > 0) {
            const int sent = send(socket, data, (int)std::min<size_t>(length, 1 << 20), 0);
            if (sent <= 0) return false;
            data += sent;
            length -= (size_t)sent;
        }
        return true;
    }

    void serve(SOCKET client);
    void acceptLoop(std::shared_ptr<ProxyState> self);
    void closeClients();
};

void ProxyState::closeClients() {
    std::lock_guard<std::mutex> lock(mutex);
    for (SOCKET socket : clients) closesocket(socket);
    clients.clear();
}

void ProxyState::acceptLoop(std::shared_ptr<ProxyState> self) {
    while (running) {
        sockaddr_in address{};
        int length = sizeof(address);
        SOCKET client = accept(listener, reinterpret_cast<sockaddr*>(&address), &length);
        if (client == INVALID_SOCKET) {
            if (!running) break;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            clients.insert(client);
        }
        std::thread([self, client] { self->serve(client); }).detach();
    }
}

void ProxyState::serve(SOCKET client) {
    // Read request headers.
    std::string request;
    char buffer[8192];
    while (request.size() < 32 * 1024) {
        const int got = recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0) break;
        request.append(buffer, (size_t)got);
        if (request.find("\r\n\r\n") != std::string::npos) break;
    }

    auto cleanup = [this, client] {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = clients.find(client);
        if (it != clients.end()) {
            clients.erase(it);
            closesocket(client);
        }
    };

    const size_t lineEnd = request.find("\r\n");
    if (lineEnd == std::string::npos) {
        cleanup();
        return;
    }
    const std::string requestLine = request.substr(0, lineEnd);
    const size_t firstSpace = requestLine.find(' ');
    const size_t secondSpace = requestLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        cleanup();
        return;
    }
    const std::string method = requestLine.substr(0, firstSpace);
    std::string target = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    const size_t query = target.find('?');
    if (query != std::string::npos) target.resize(query);

    const std::string prefix = "/file/";
    ProxyFile file;
    bool found = false;
    if (target.rfind(prefix, 0) == 0) {
        const std::string token = target.substr(prefix.size());
        std::lock_guard<std::mutex> lock(mutex);
        auto it = files.find(token);
        if (it != files.end()) {
            file = it->second;
            found = true;
        }
    }
    if (!found) {
        const char* notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        SendAll(client, notFound, strlen(notFound));
        cleanup();
        return;
    }

    // Range: the proxy supports one byte range, which is what aria2 sends.
    long long start = 0;
    long long end = file.size > 0 ? file.size - 1 : -1;
    bool partial = false;
    const std::string range = headerValue(request, "Range");
    if (range.rfind("bytes=", 0) == 0) {
        const std::string value = range.substr(6);
        if (value.find(',') == std::string::npos) {
            const size_t dash = value.find('-');
            if (dash != std::string::npos) {
                const std::string startText = value.substr(0, dash);
                const std::string endText = value.substr(dash + 1);
                if (!startText.empty()) {
                    start = _strtoi64(startText.c_str(), nullptr, 10);
                    partial = true;
                }
                if (!endText.empty()) end = _strtoi64(endText.c_str(), nullptr, 10);
            }
        }
    }
    if (start < 0) start = 0;
    if (file.size > 0 && start > file.size - 1) {
        const char* badRange =
            "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        SendAll(client, badRange, strlen(badRange));
        cleanup();
        return;
    }
    if (file.size > 0 && (end < start || end >= file.size)) end = file.size - 1;
    const long long total = file.size;

    // Open the MEGA download and ask for the same byte range.
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t path[8192]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = (DWORD)std::size(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = (DWORD)std::size(path);

    HINTERNET session = nullptr, connect = nullptr, remote = nullptr;
    bool remoteOk = false;
    long long remoteStart = 0;

    if (WinHttpCrackUrl(file.remote.c_str(), (DWORD)file.remote.size(), 0, &parts)) {
        std::wstring remoteTarget(path, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength && parts.lpszExtraInfo)
            remoteTarget.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        if (remoteTarget.empty()) remoteTarget = L"/";

        session = WinHttpOpen(L"Aria2DM-Mega/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session) {
            WinHttpSetTimeouts(session, 15000, 15000, 20000, 30000);
            connect = WinHttpConnect(session, std::wstring(host, parts.dwHostNameLength).c_str(),
                                     parts.nPort, 0);
        }
        if (connect) {
            const DWORD flags =
                parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
            remote = WinHttpOpenRequest(connect, L"GET", remoteTarget.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        }
        if (remote) {
            DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(remote, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
            std::wstring rangeHeader;
            if (partial) {
                rangeHeader = L"Range: bytes=" + std::to_wstring(start) + L"-";
                if (end >= start) rangeHeader += std::to_wstring(end);
                rangeHeader += L"\r\n";
            }
            const wchar_t* headers = rangeHeader.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                                         : rangeHeader.c_str();
            if (WinHttpSendRequest(remote, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(remote, nullptr)) {
                DWORD status = 0;
                DWORD size = sizeof(status);
                WinHttpQueryHeaders(remote, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                    WINHTTP_NO_HEADER_INDEX);
                if (status == 200 || status == 206) {
                    remoteOk = true;
                    if (status == 206) {
                        remoteStart = start;
                    } else {
                        remoteStart = 0;
                        partial = start > 0;
                    }
                }
            }
        }
    }

    if (!remoteOk) {
        const char* failure = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        SendAll(client, failure, strlen(failure));
        if (remote) WinHttpCloseHandle(remote);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
        cleanup();
        return;
    }

    long long sendLength = (end >= start) ? end - start + 1 : -1;
    if (total > 0) sendLength = std::min(sendLength < 0 ? total - start : sendLength, total - start);

    std::wstring header;
    header += partial ? L"HTTP/1.1 206 Partial Content\r\n" : L"HTTP/1.1 200 OK\r\n";
    header += L"Content-Type: application/octet-stream\r\n";
    header += L"Accept-Ranges: bytes\r\n";
    header += L"Connection: close\r\n";
    header += L"Content-Disposition: attachment; filename=\"" + AsciiFilename(file.name) + L"\"\r\n";
    if (total > 0) {
        header += L"Content-Length: " + std::to_wstring(sendLength) + L"\r\n";
        if (partial)
            header += L"Content-Range: bytes " + std::to_wstring(start) + L"-" +
                      std::to_wstring(end) + L"/" + std::to_wstring(total) + L"\r\n";
    }
    header += L"\r\n";
    const std::string headerNarrow = util::Narrow(header);
    if (!SendAll(client, headerNarrow.data(), headerNarrow.size()) || method == "HEAD") {
        if (remote) WinHttpCloseHandle(remote);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
        cleanup();
        return;
    }

    // Stream and decrypt. remoteStart is the file offset of the next byte we
    // will read from MEGA; `start` is the first byte the client asked for.
    long long offset = remoteStart;
    long long skip = start - remoteStart;
    long long remaining = sendLength;
    std::vector<unsigned char> chunk(64 * 1024);
    bool ok = true;
    while (remaining != 0) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(remote, &available) || available == 0) {
            ok = false;
            break;
        }
        if (available > chunk.size()) available = (DWORD)chunk.size();
        DWORD read = 0;
        if (!WinHttpReadData(remote, chunk.data(), available, &read) || read == 0) {
            ok = false;
            break;
        }
        unsigned char* data = chunk.data();
        size_t length = read;
        if (!MegaCtrXor(file.key, (uint64_t)offset, data, length)) {
            ok = false;
            break;
        }
        offset += (long long)length;

        if (skip > 0) {
            const size_t drop = (size_t)std::min<long long>(skip, (long long)length);
            data += drop;
            length -= drop;
            skip -= (long long)drop;
        }
        if (length == 0) continue;
        if (remaining > 0 && (long long)length > remaining) length = (size_t)remaining;
        if (!SendAll(client, reinterpret_cast<const char*>(data), length)) {
            ok = false;
            break;
        }
        if (remaining > 0) remaining -= (long long)length;
    }

    (void)ok;
    if (remote) WinHttpCloseHandle(remote);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    cleanup();
}

} // namespace

struct MegaProxy::Impl {
    std::shared_ptr<ProxyState> state;
    std::thread acceptThread;

    bool start(std::wstring* error) {
        if (!state) state = std::make_shared<ProxyState>();
        if (state->running) return true;

        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            if (error) *error = L"Could not initialize the local network stack.";
            return false;
        }
        state->wsa = true;
        state->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (state->listener == INVALID_SOCKET) {
            if (error) *error = L"Could not create the local MEGA proxy.";
            WSACleanup();
            state->wsa = false;
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(state->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(state->listener, SOMAXCONN) != 0) {
            if (error) *error = L"Could not open a port for the MEGA proxy.";
            closesocket(state->listener);
            state->listener = INVALID_SOCKET;
            WSACleanup();
            state->wsa = false;
            return false;
        }
        sockaddr_in bound{};
        int length = sizeof(bound);
        getsockname(state->listener, reinterpret_cast<sockaddr*>(&bound), &length);
        state->port = ntohs(bound.sin_port);
        state->running = true;
        std::shared_ptr<ProxyState> self = state;
        acceptThread = std::thread([self] { self->acceptLoop(self); });
        return true;
    }

    void stop() {
        if (!state) return;
        state->running = false;
        if (state->listener != INVALID_SOCKET) {
            closesocket(state->listener);
            state->listener = INVALID_SOCKET;
        }
        state->closeClients();
        if (acceptThread.joinable()) acceptThread.join();
        if (state->wsa) {
            WSACleanup();
            state->wsa = false;
        }
    }
};

MegaProxy::MegaProxy() : impl_(std::make_unique<Impl>()) {}

MegaProxy::~MegaProxy() {
    stop();
}

bool MegaProxy::start(std::wstring* error) {
    return impl_ && impl_->start(error);
}

void MegaProxy::stop() {
    if (impl_) impl_->stop();
}

bool MegaProxy::running() const {
    return impl_ && impl_->state && impl_->state->running;
}

std::wstring MegaProxy::addFile(const std::wstring& remoteUrl,
                                const std::vector<unsigned char>& key, long long size,
                                const std::wstring& name) {
    if (!impl_ || !impl_->state || !impl_->state->running) return {};
    return impl_->state->addFile(remoteUrl, key, size, name);
}
