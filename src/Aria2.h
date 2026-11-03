#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>

struct Aria2Item {
    std::wstring gid;
    std::wstring status;   // active, waiting, paused, complete, error, removed
    std::wstring name;
    std::wstring uri;
    std::wstring dir;
    std::wstring path;
    std::wstring errorMessage;
    std::wstring followedBy; // non-empty when this item spawned another (torrent metadata)
    long long total = 0;
    long long completed = 0;
    long long downSpeed = 0;
    long long upSpeed = 0;
    int connections = 0;
    int errorCode = 0;
    int numSeeders = 0;
    bool isTorrent = false;
    bool seeder = false;

    bool isActive() const { return status == L"active"; }
    bool isWaiting() const { return status == L"waiting"; }
    bool isPaused() const { return status == L"paused"; }
    bool isComplete() const { return status == L"complete"; }
    bool isError() const { return status == L"error"; }
    bool isRemoved() const { return status == L"removed"; }
    bool isFinished() const { return isComplete() || isError() || isRemoved(); }
    // A magnet or .torrent link produces a short-lived "download" that only
    // fetches the torrent metadata and then hands over to a second GID with the
    // actual files. That placeholder must not be listed as a finished download.
    bool isMetadataPlaceholder() const { return isTorrent && !followedBy.empty(); }
    double progress() const {
        if (total <= 0) return isComplete() ? 1.0 : 0.0;
        double p = (double)completed / (double)total;
        return p < 0 ? 0 : (p > 1 ? 1 : p);
    }
};

enum class EngineState { Starting, Connected, Error };

struct Aria2Snapshot {
    std::vector<Aria2Item> items;
    long long downSpeed = 0;
    long long upSpeed = 0;
    int active = 0;
    int waiting = 0;
    int stopped = 0;
    EngineState state = EngineState::Starting;
    std::wstring message;
    std::wstring version;
    int port = 0;
};

// Owns a private aria2c process and talks to its JSON-RPC endpoint over WinHTTP.
// All network and process work happens on a dedicated worker thread.
class Aria2Client {
public:
    using SnapshotFn = std::function<void(const Aria2Snapshot&)>;
    using NotifyFn = std::function<void(const std::wstring&)>;

    Aria2Client();
    ~Aria2Client();

    Aria2Client(const Aria2Client&) = delete;
    Aria2Client& operator=(const Aria2Client&) = delete;

    void start(std::wstring exeDir, std::wstring dataDir, std::wstring downloadDir, int maxConcurrent,
               long long speedLimit, SnapshotFn onSnapshot, NotifyFn onNotify);
    void stop();
    bool running() const;

    void addUri(std::wstring url, std::wstring dir, std::wstring out,
                bool singleConnection = false);
    void pauseItem(std::wstring gid, bool pause);
    void removeItem(std::wstring gid, std::wstring status);
    void retryItem(std::wstring gid, std::wstring status, std::wstring uri, std::wstring dir, std::wstring name,
                   bool torrent = false);
    void clearCompleted();
    void applyOptions(int maxConcurrent, long long speedLimit, std::wstring dir);
    void retryEngine();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
