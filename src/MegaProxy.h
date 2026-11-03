#pragma once

#include <memory>
#include <string>
#include <vector>

// Serves MEGA files over a loopback HTTP URL while decrypting AES-CTR on the
// fly. aria2 can then download a MEGA link like any other URL, including
// resuming and using Range requests.
class MegaProxy {
public:
    MegaProxy();
    ~MegaProxy();

    MegaProxy(const MegaProxy&) = delete;
    MegaProxy& operator=(const MegaProxy&) = delete;

    bool start(std::wstring* error = nullptr);
    void stop();
    bool running() const;

    // Registers a file and returns http://127.0.0.1:<port>/file/<token>.
    std::wstring addFile(const std::wstring& remoteUrl, const std::vector<unsigned char>& key,
                         long long size, const std::wstring& name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
