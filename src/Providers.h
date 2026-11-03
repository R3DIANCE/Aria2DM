#pragma once

#include <string>
#include <vector>

// Which service a link belongs to. Everything else is treated as a plain URL.
enum class Provider { Direct, GoogleDrive, MediaFire, Mega, MegaFolder };

struct ResolvedLink {
    Provider provider = Provider::Direct;
    std::wstring url;       // direct URL; empty for MEGA (the proxy builds one)
    std::wstring filename;  // suggested output name, may be empty
    std::wstring error;     // non-empty when resolution failed
    std::wstring handle;    // MEGA file handle
    std::vector<unsigned char> megaKey; // 32-byte MEGA file key (AES key + nonce)
    std::wstring megaRemote;            // temporary MEGA download URL
    long long size = 0;
    bool needsKey = false;  // MEGA link without a decryption key
};

Provider DetectProvider(const std::wstring& url);
bool IsMegaLink(const std::wstring& url);
bool IsGoogleDriveLink(const std::wstring& url);
bool IsMediaFireLink(const std::wstring& url);

// Rewrites a Google Drive "view" link into a direct download URL. Returns an
// empty string when the input is not a Drive link. No network access.
std::wstring NormalizeDriveUrl(const std::wstring& url);

// Resolves a link into something aria2 can download. Network calls are made
// synchronously, so call this from a worker thread. `suppliedKey` is the MEGA
// decryption key typed by the user (or a full MEGA link that contains one).
ResolvedLink ResolveLink(const std::wstring& url, const std::wstring& suppliedKey = {});

// Small helpers shared with the browser bridge and the MEGA proxy.
std::string Base64UrlDecode(const std::string& text);
std::string Base64Decode(const std::string& text);
std::wstring HttpFilenameFromHeaders(const std::wstring& disposition);

// XORs `data` with the MEGA AES-CTR keystream for the file at `offset`.
bool MegaCtrXor(const std::vector<unsigned char>& fileKey, uint64_t offset,
                unsigned char* data, size_t length);
