#include "Providers.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

#include "Json.h"
#include "Util.h"

namespace {

constexpr wchar_t kBrowserUserAgent[] =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    L"(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

struct HttpResponse {
    DWORD status = 0;
    std::string body;
    std::wstring disposition;
    std::wstring contentType;
    std::wstring location;
    long long contentLength = -1;
};

std::wstring HeaderValue(const std::wstring& raw, const wchar_t* name) {
    const std::wstring lowered = util::Lower(raw);
    const std::wstring key = util::Lower(name);
    size_t pos = 0;
    while ((pos = lowered.find(L"\r\n" + key + L":", pos)) != std::wstring::npos) {
        pos += 2 + key.size() + 1;
        size_t end = raw.find(L"\r\n", pos);
        return util::Trim(raw.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos));
    }
    // The first header line has no leading CRLF.
    if (lowered.rfind(key + L":", 0) == 0) {
        size_t pos2 = key.size() + 1;
        size_t end = raw.find(L"\r\n", pos2);
        return util::Trim(raw.substr(pos2, end == std::wstring::npos ? std::wstring::npos : end - pos2));
    }
    return {};
}

bool HttpRequest(const std::wstring& method, const std::wstring& url, const std::string& body,
                 size_t maxBody, HttpResponse& out, std::wstring& error, bool readBody = true) {
    out = {};
    error.clear();

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t path[8192]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = (DWORD)std::size(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = (DWORD)std::size(path);
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &parts)) {
        error = L"Invalid URL.";
        return false;
    }

    std::wstring target(path, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength && parts.lpszExtraInfo) {
        target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    if (target.empty()) target = L"/";

    HINTERNET session = WinHttpOpen(kBrowserUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = L"Could not initialize WinHTTP.";
        return false;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);

    HINTERNET connect =
        WinHttpConnect(session, std::wstring(host, parts.dwHostNameLength).c_str(), parts.nPort, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        error = L"Could not reach the server.";
        return false;
    }

    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, method.c_str(), target.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = L"Could not create the request.";
        return false;
    }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    const wchar_t* headers = L"Accept: */*\r\nAccept-Language: en-US,en;q=0.9\r\n"
                             L"Accept-Encoding: identity\r\nCache-Control: no-cache\r\n";
    const void* bodyData = body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data();
    DWORD bodySize = (DWORD)body.size();
    BOOL sent = WinHttpSendRequest(request, headers, (DWORD)-1L,
                                   const_cast<void*>(bodyData), bodySize, bodySize, 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        error = L"The server did not respond.";
        return false;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    out.status = status;

    DWORD headerSize = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                        nullptr, &headerSize, WINHTTP_NO_HEADER_INDEX);
    if (headerSize > 0) {
        std::wstring raw(headerSize / sizeof(wchar_t) + 1, L'\0');
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &headerSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            raw.resize(wcslen(raw.c_str()));
            out.disposition = HeaderValue(raw, L"Content-Disposition");
            out.contentType = HeaderValue(raw, L"Content-Type");
            out.location = HeaderValue(raw, L"Location");
            const std::wstring lengthText = HeaderValue(raw, L"Content-Length");
            if (!lengthText.empty()) out.contentLength = _wcstoi64(lengthText.c_str(), nullptr, 10);
        }
    }

    if (readBody && out.status < 400) {
        DWORD available = 0;
        while (out.body.size() < maxBody &&
               WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
            chunk.resize(read);
            if (out.body.size() + chunk.size() > maxBody) {
                out.body.append(chunk.data(), maxBody - out.body.size());
                break;
            }
            out.body += chunk;
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return true;
}

std::string HtmlDecode(std::string text) {
    struct Entity {
        const char* from;
        char to;
    };
    static const Entity entities[] = {{"&amp;", '&'}, {"&#38;", '&'}, {"&quot;", '"'},
                                      {"&#34;", '"'}, {"&#39;", '\''}, {"&lt;", '<'},
                                      {"&gt;", '>'}};
    for (const Entity& entity : entities) {
        size_t pos = 0;
        while ((pos = text.find(entity.from, pos)) != std::string::npos) {
            text.replace(pos, strlen(entity.from), 1, entity.to);
            ++pos;
        }
    }
    return text;
}

std::wstring DataUrl(const std::wstring& url) {
    if (url.rfind(L"//", 0) == 0) return L"https:" + url;
    if (url.rfind(L"http://", 0) == 0 || url.rfind(L"https://", 0) == 0) return url;
    return {};
}

std::wstring QueryParam(const std::wstring& url, const wchar_t* name) {
    std::wstring needle = std::wstring(L"?") + name + L"=";
    size_t pos = url.find(needle);
    if (pos == std::wstring::npos) {
        needle = std::wstring(L"&") + name + L"=";
        pos = url.find(needle);
    }
    if (pos == std::wstring::npos) return {};
    pos += needle.size();
    size_t end = url.find_first_of(L"&#", pos);
    return url.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
}

std::string PercentDecode(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(text[i + 1]);
            const int lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// AES helpers (CNG). Only what MEGA needs: ECB blocks, CBC attribute decrypt
// and the AES-CTR keystream used by the local decrypting proxy.
// ---------------------------------------------------------------------------

bool AesEcb(const std::vector<unsigned char>& key, const unsigned char* input, size_t length,
            bool encrypt, std::vector<unsigned char>& output) {
    output.assign(length, 0);
    if (key.size() != 16 && key.size() != 24 && key.size() != 32) return false;

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return false;
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                      sizeof(BCRYPT_CHAIN_MODE_ECB), 0);

    DWORD objectSize = 0, result = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objectSize, sizeof(objectSize),
                          &result, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }
    std::vector<unsigned char> object(objectSize);
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    if (BCryptGenerateSymmetricKey(alg, &keyHandle, object.data(), objectSize,
                                   (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return false;
    }

    DWORD written = 0;
    NTSTATUS status = encrypt
                          ? BCryptEncrypt(keyHandle, (PUCHAR)input, (ULONG)length, nullptr, nullptr,
                                          0, output.data(), (ULONG)output.size(), &written, 0)
                          : BCryptDecrypt(keyHandle, (PUCHAR)input, (ULONG)length, nullptr, nullptr,
                                          0, output.data(), (ULONG)output.size(), &written, 0);
    BCryptDestroyKey(keyHandle);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (status != 0) return false;
    output.resize(written);
    return true;
}

void CtrAdd(std::vector<unsigned char>& counter, uint64_t blocks) {
    // Big-endian 128-bit counter.
    uint64_t low = 0;
    for (int i = 8; i < 16; ++i) low = (low << 8) | counter[i];
    const uint64_t baseline = low;
    uint64_t sum = low + blocks;
    uint64_t high = 0;
    for (int i = 0; i < 8; ++i) high = (high << 8) | counter[i];
    if (sum < baseline) high += 1;
    for (int i = 7; i >= 0; --i) {
        counter[i] = (unsigned char)(high & 0xFF);
        high >>= 8;
    }
    for (int i = 15; i >= 8; --i) {
        counter[i] = (unsigned char)(sum & 0xFF);
        sum >>= 8;
    }
}

bool AesCtrXor(const std::vector<unsigned char>& key16, const unsigned char* nonce,
               uint64_t offset, unsigned char* data, size_t length) {
    if (length == 0) return true;
    const uint64_t blockIndex = offset / 16;
    size_t skip = (size_t)(offset % 16);
    std::vector<unsigned char> counter(nonce, nonce + 16);
    CtrAdd(counter, blockIndex);

    constexpr size_t kBlocksPerBatch = 512; // 8 KiB of keystream per CNG call
    std::vector<unsigned char> counters;
    std::vector<unsigned char> stream;
    while (length > 0) {
        const size_t blocks =
            std::min<size_t>((length + skip + 15) / 16, kBlocksPerBatch);
        counters.resize(blocks * 16);
        for (size_t b = 0; b < blocks; ++b) {
            std::memcpy(counters.data() + b * 16, counter.data(), 16);
            for (int i = 15; i >= 0; --i) {
                if (++counter[i] != 0) break;
            }
        }
        if (!AesEcb(key16, counters.data(), counters.size(), true, stream)) return false;
        const size_t take = std::min<size_t>(length, blocks * 16 - skip);
        for (size_t i = 0; i < take; ++i) data[i] ^= stream[skip + i];
        data += take;
        length -= take;
        skip = 0;
    }
    return true;
}

std::vector<unsigned char> AesCbcDecrypt(const std::vector<unsigned char>& key,
                                         const std::vector<unsigned char>& cipher) {
    std::vector<unsigned char> plain;
    if (cipher.empty() || cipher.size() % 16 != 0) return plain;
    std::vector<unsigned char> iv(16, 0);
    for (size_t offset = 0; offset < cipher.size(); offset += 16) {
        std::vector<unsigned char> block;
        if (!AesEcb(key, cipher.data() + offset, 16, false, block)) return {};
        for (size_t i = 0; i < 16; ++i) plain.push_back(block[i] ^ iv[i]);
        iv.assign(cipher.begin() + offset, cipher.begin() + offset + 16);
    }
    // PKCS#7 padding.
    if (!plain.empty()) {
        const size_t pad = plain.back();
        if (pad > 0 && pad <= 16 && pad <= plain.size()) {
            bool valid = true;
            for (size_t i = plain.size() - pad; i < plain.size(); ++i)
                if (plain[i] != pad) valid = false;
            if (valid) plain.resize(plain.size() - pad);
        }
    }
    return plain;
}

std::string JsonUnescape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        const char next = text[++i];
        switch (next) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
            if (i + 4 >= text.size()) break;
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            unsigned value = 0;
            for (int k = 1; k <= 4; ++k) value = (value << 4) | (unsigned)hex(text[i + k]);
            i += 4;
            if (value < 0x80) {
                out.push_back((char)value);
            } else if (value < 0x800) {
                out.push_back((char)(0xC0 | (value >> 6)));
                out.push_back((char)(0x80 | (value & 0x3F)));
            } else {
                out.push_back((char)(0xE0 | (value >> 12)));
                out.push_back((char)(0x80 | ((value >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (value & 0x3F)));
            }
            break;
        }
        default: out.push_back(next); break;
        }
    }
    return out;
}

// MEGA file attributes are AES-128-CBC encrypted JSON. The 32-byte file key
// splits into two halves; the attribute key is half0 XOR half1 and the IV is
// zero. Plaintext is zero padded and looks like MEGA{"n":"name"}.
std::wstring MegaDecryptName(const std::vector<unsigned char>& fileKey,
                             const std::string& attributesB64) {
    if (fileKey.size() < 32 || attributesB64.empty()) return {};
    const std::string cipherText = Base64UrlDecode(attributesB64);
    if (cipherText.empty() || cipherText.size() % 16 != 0) return {};
    std::vector<unsigned char> cipher(cipherText.begin(), cipherText.end());
    std::vector<unsigned char> key(fileKey.begin(), fileKey.begin() + 16);
    for (size_t i = 0; i < 16; ++i) key[i] ^= fileKey[i + 16];
    const std::vector<unsigned char> plain = AesCbcDecrypt(key, cipher);
    if (plain.empty()) return {};
    const std::string text(plain.begin(), plain.end());
    size_t namePos = text.find("\"n\"");
    if (namePos == std::string::npos) return {};
    namePos = text.find(':', namePos);
    if (namePos == std::string::npos) return {};
    namePos = text.find('"', namePos);
    if (namePos == std::string::npos) return {};
    size_t end = namePos + 1;
    while (end < text.size()) {
        if (text[end] == '\\') {
            end += 2;
            continue;
        }
        if (text[end] == '"') break;
        ++end;
    }
    if (end >= text.size()) return {};
    return util::Widen(JsonUnescape(text.substr(namePos + 1, end - namePos - 1)));
}

// ---------------------------------------------------------------------------
// Provider parsing
// ---------------------------------------------------------------------------

std::wstring DriveFileId(const std::wstring& url) {
    std::wstring id = QueryParam(url, L"id");
    if (!id.empty()) return id;
    const std::wstring marker = L"/file/d/";
    size_t pos = url.find(marker);
    if (pos == std::wstring::npos) return {};
    pos += marker.size();
    size_t end = url.find_first_of(L"/?#", pos);
    return url.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
}

bool ParseMegaLink(const std::wstring& url, std::wstring& handle, std::wstring& key, bool& folder) {
    handle.clear();
    key.clear();
    folder = false;

    const std::wstring lower = util::Lower(url);
    size_t pos = lower.find(L"mega.nz/");
    if (pos == std::wstring::npos) {
        if (lower.rfind(L"mega:", 0) != 0) return false;
        pos = 0;
    } else {
        pos += 8;
    }

    const std::wstring rest = url.substr(pos);
    if (rest.rfind(L"file/", 0) == 0 || rest.rfind(L"folder/", 0) == 0) {
        folder = rest.rfind(L"folder/", 0) == 0;
        const size_t start = folder ? 7 : 5;
        size_t end = rest.find_first_of(L"/?#", start);
        handle = rest.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        const size_t hash = rest.find(L'#');
        if (hash != std::wstring::npos) {
            key = rest.substr(hash + 1);
            const size_t cut = key.find_first_of(L"/?&");
            if (cut != std::wstring::npos) key.resize(cut);
        }
    } else if (rest.rfind(L"#!", 0) == 0 || rest.rfind(L"#F!", 0) == 0) {
        folder = rest.rfind(L"#F!", 0) == 0;
        const size_t start = folder ? 3 : 2;
        const size_t bang = rest.find(L'!', start);
        handle = rest.substr(start, bang == std::wstring::npos ? std::wstring::npos : bang - start);
        if (bang != std::wstring::npos) {
            key = rest.substr(bang + 1);
            const size_t cut = key.find_first_of(L"/?&");
            if (cut != std::wstring::npos) key.resize(cut);
        }
    } else {
        return false;
    }

    if (handle.size() > 8) handle.resize(8);
    return !handle.empty();
}

std::wstring CleanMegaKey(std::wstring value) {
    value = util::Trim(value);
    if (value.empty()) return value;
    // A user may paste the whole link instead of just the part after '#'.
    if (util::ContainsNoCase(value, L"mega.nz/")) {
        std::wstring handle, key;
        bool folder = false;
        if (ParseMegaLink(value, handle, key, folder)) return key;
    }
    if (!value.empty() && value[0] == L'#') value.erase(value.begin());
    if (!value.empty() && value[0] == L'!') value.erase(value.begin());
    const size_t cut = value.find_first_of(L" \t\r\n/?#&");
    if (cut != std::wstring::npos) value.resize(cut);
    return value;
}

ResolvedLink ResolveDrive(const std::wstring& url) {
    ResolvedLink result;
    result.provider = Provider::GoogleDrive;
    const std::wstring id = DriveFileId(url);
    if (id.empty()) {
        result.error = L"That Google Drive link does not contain a file id.";
        return result;
    }
    std::wstring direct = L"https://drive.usercontent.google.com/download?id=" + id +
                          L"&export=download&confirm=t";
    const std::wstring resourceKey = QueryParam(url, L"resourcekey");
    if (!resourceKey.empty()) direct += L"&resourcekey=" + resourceKey;
    result.url = direct;

    HttpResponse head;
    std::wstring error;
    if (HttpRequest(L"HEAD", direct, {}, 0, head, error, false) && head.status < 400) {
        result.filename = HttpFilenameFromHeaders(head.disposition);
        if (head.contentLength > 0) result.size = head.contentLength;
    }
    return result;
}

std::wstring MediaFireName(const std::wstring& url) {
    const std::wstring lower = util::Lower(url);
    size_t pos = lower.find(L"mediafire.com/file/");
    if (pos != std::wstring::npos) {
        pos += 19; // length of "mediafire.com/file/"
        const size_t slash = url.find(L'/', pos);
        if (slash == std::wstring::npos) return {};
        size_t end = url.find_first_of(L"/?#", slash + 1);
        std::wstring name = url.substr(slash + 1, end == std::wstring::npos ? std::wstring::npos
                                                                            : end - slash - 1);
        if (name == L"file") return {};
        return name;
    }
    return {};
}

ResolvedLink ResolveMediaFire(const std::wstring& url) {
    ResolvedLink result;
    result.provider = Provider::MediaFire;
    result.filename = MediaFireName(url);

    HttpResponse page;
    std::wstring error;
    if (!HttpRequest(L"GET", url, {}, 3 * 1024 * 1024, page, error)) {
        result.error = error;
        return result;
    }
    if (page.status >= 400) {
        result.error = L"MediaFire returned an error (HTTP " + std::to_wstring(page.status) + L").";
        return result;
    }

    const std::string html = HtmlDecode(page.body);
    std::string candidate;

    // Only accept the real download host (download####.mediafire.com). The
    // page itself contains many links containing the word "download", such as
    // download_repair.php, which must not be mistaken for the file.
    auto isDirect = [](const std::string& value) {
        std::string url = value;
        if (url.rfind("//", 0) == 0) url = "https:" + url;
        const size_t scheme = url.find("://");
        if (scheme == std::string::npos) return false;
        const size_t hostStart = scheme + 3;
        const size_t slash = url.find('/', hostStart);
        std::string host = url.substr(hostStart, slash == std::string::npos ? std::string::npos
                                                                           : slash - hostStart);
        for (char& c : host) c = (char)tolower((unsigned char)c);
        return host.find("mediafire.com") != std::string::npos &&
               host.rfind("download", 0) == 0;
    };

    auto takeHref = [&html](size_t start, size_t limit) {
        size_t href = html.find("href=", start);
        if (href == std::string::npos || href > limit) return std::string();
        href += 5;
        if (href >= html.size()) return std::string();
        const char quote = html[href];
        if (quote == '"' || quote == '\'') ++href;
        size_t end = href;
        while (end < html.size() && html[end] != '"' && html[end] != '\'' && html[end] != '<' &&
               html[end] != ' ' && html[end] != '\r' && html[end] != '\n')
            ++end;
        return html.substr(href, end - href);
    };

    // The download button carries the direct link in its href.
    size_t button = html.find("id=\"downloadButton\"");
    if (button == std::string::npos) button = html.find("id='downloadButton'");
    if (button != std::string::npos) {
        candidate = takeHref(button, button + 800);
        if (!isDirect(candidate)) candidate.clear();
    }

    if (candidate.empty()) {
        size_t pos = 0;
        while ((pos = html.find("download", pos)) != std::string::npos) {
            size_t scheme = html.rfind("https://", pos);
            if (scheme == std::string::npos || pos - scheme > 32) {
                scheme = html.rfind("//download", pos);
                if (scheme != std::string::npos) {
                    size_t end = scheme;
                    while (end < html.size() && html[end] != '"' && html[end] != '\'' &&
                           html[end] != '<' && html[end] != ' ' && html[end] != '\r' &&
                           html[end] != '\n')
                        ++end;
                    std::string found = html.substr(scheme + 2, end - scheme - 2);
                    if (isDirect(found)) {
                        candidate = found;
                        break;
                    }
                }
                pos += 8;
                continue;
            }
            size_t end = scheme;
            while (end < html.size() && html[end] != '"' && html[end] != '\'' && html[end] != '<' &&
                   html[end] != ' ' && html[end] != '\r' && html[end] != '\n')
                ++end;
            std::string found = html.substr(scheme, end - scheme);
            if (isDirect(found)) {
                candidate = found;
                break;
            }
            pos += 8;
        }
    }

    if (candidate.empty()) {
        size_t redirect = html.find("window.location.href");
        if (redirect == std::string::npos) redirect = html.find("location.href");
        if (redirect != std::string::npos) {
            size_t quote = html.find_first_of("'\"", redirect);
            if (quote != std::string::npos) {
                size_t end = html.find(html[quote], quote + 1);
                if (end != std::string::npos && end > quote + 1) {
                    candidate = html.substr(quote + 1, end - quote - 1);
                    if (!isDirect(candidate)) candidate.clear();
                }
            }
        }
    }

    if (candidate.empty()) {
        result.error = L"Could not find the MediaFire download link.";
        return result;
    }

    std::string cleaned = candidate;
    if (cleaned.rfind("//", 0) == 0) cleaned = "https:" + cleaned;
    result.url = util::Widen(cleaned);
    if (result.filename.empty()) {
        HttpResponse head;
        std::wstring ignored;
        if (HttpRequest(L"HEAD", result.url, {}, 0, head, ignored, false) && head.status < 400) {
            result.filename = HttpFilenameFromHeaders(head.disposition);
            if (head.contentLength > 0) result.size = head.contentLength;
        }
    }
    return result;
}

ResolvedLink ResolveMega(const std::wstring& url, const std::wstring& suppliedKey) {
    ResolvedLink result;
    result.provider = Provider::Mega;

    std::wstring handle, key;
    bool folder = false;
    if (!ParseMegaLink(url, handle, key, folder)) {
        result.error = L"That does not look like a MEGA link.";
        return result;
    }
    result.handle = handle;
    if (folder) {
        result.provider = Provider::MegaFolder;
        result.error = L"MEGA folder links aren't supported. Open the folder and add its files individually.";
        return result;
    }

    if (!suppliedKey.empty()) key = CleanMegaKey(suppliedKey);
    key = CleanMegaKey(key);
    if (key.empty()) {
        result.needsKey = true;
        result.error = L"This MEGA link needs its decryption key.";
        return result;
    }

    const std::string decoded = Base64UrlDecode(util::Narrow(key));
    if (decoded.size() != 32) {
        result.needsKey = true;
        result.error = L"That MEGA key is not valid. Paste the full link or the 43-character key.";
        return result;
    }
    result.megaKey.assign(decoded.begin(), decoded.end());

    const std::wstring endpoint = L"https://g.api.mega.co.nz/cs?id=" +
                                  std::to_wstring((unsigned)(GetTickCount() ^ (GetCurrentProcessId() << 8))) +
                                  L"&n=" + handle;
    const std::string requestBody =
        "[{\"a\":\"g\",\"g\":1,\"p\":\"" + util::Narrow(handle) + "\"}]";

    HttpResponse response;
    std::wstring error;
    if (!HttpRequest(L"POST", endpoint, requestBody, 1024 * 1024, response, error)) {
        result.error = error;
        return result;
    }
    if (response.status >= 400) {
        result.error = L"MEGA returned HTTP " + std::to_wstring(response.status) + L".";
        return result;
    }

    Json parsed = Json::Parse(response.body, nullptr);

    // MEGA returns either [{"e":<code>}] or a bare number for failures.
    long long code = 0;
    Json entry = Json::Object();
    if (parsed.isArray() && parsed.size() > 0) {
        entry = parsed[0];
        code = entry["e"].asInt(0);
    } else if (parsed.type() == Json::Type::Number) {
        code = parsed.asInt(0);
    } else {
        result.error = L"MEGA returned an unexpected response.";
        return result;
    }
    if (code != 0) {
        switch (code) {
        case -9:
            result.error = L"The MEGA file no longer exists.";
            break;
        case -16:
            result.needsKey = true;
            result.error = L"This MEGA link is password protected. Enter its decryption key.";
            break;
        case -17:
            result.error = L"MEGA is rate limiting requests. Try again in a minute.";
            break;
        default:
            result.error = L"MEGA error " + std::to_wstring(code) + L".";
            break;
        }
        return result;
    }

    const std::string remote = entry["g"].asString();
    if (remote.empty()) {
        result.error = L"MEGA did not provide a download URL.";
        return result;
    }
    result.megaRemote = util::Widen(remote);
    result.size = entry["s"].asInt(0);
    result.filename = MegaDecryptName(result.megaKey, entry["at"].asString());
    return result;
}

} // namespace

std::string Base64Decode(const std::string& text) {
    auto decode = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    out.reserve(text.size() * 3 / 4);
    int value = 0, bits = -8;
    for (unsigned char c : text) {
        if (c == '=') break;
        const int decoded = decode((char)c);
        if (decoded < 0) continue;
        value = (value << 6) | decoded;
        bits += 6;
        if (bits >= 0) {
            out.push_back((char)((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

std::string Base64UrlDecode(const std::string& text) {
    std::string normalized = text;
    for (char& c : normalized) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    return Base64Decode(normalized);
}

std::wstring HttpFilenameFromHeaders(const std::wstring& disposition) {
    if (disposition.empty()) return {};
    std::wstring value = disposition;

    // RFC 5987 extended value wins because it is UTF-8 and unambiguous.
    size_t extended = util::Lower(value).find(L"filename*=");
    if (extended != std::wstring::npos) {
        size_t start = extended + 10;
        size_t end = value.find(L';', start);
        std::wstring part = value.substr(start, end == std::wstring::npos ? std::wstring::npos
                                                                          : end - start);
        size_t quote = part.find_first_of(L"'\u0027");
        if (quote != std::wstring::npos) part = part.substr(quote + 1);
        quote = part.find(L'\'');
        if (quote != std::wstring::npos) part = part.substr(quote + 1);
        part = util::Widen(PercentDecode(util::Narrow(util::Trim(part))));
        if (!part.empty()) return util::FileName(part);
    }

    size_t pos = util::Lower(value).find(L"filename=");
    if (pos == std::wstring::npos) return {};
    pos += 9;
    while (pos < value.size() && value[pos] == L' ') ++pos;
    std::wstring name;
    if (pos < value.size() && (value[pos] == L'"' || value[pos] == L'\'')) {
        const wchar_t quote = value[pos++];
        const size_t end = value.find(quote, pos);
        name = value.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
    } else {
        size_t end = value.find(L';', pos);
        name = value.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
    }
    name = util::Widen(PercentDecode(util::Narrow(util::Trim(name))));
    return util::FileName(name);
}

Provider DetectProvider(const std::wstring& url) {
    const std::wstring lower = util::Lower(url);
    if (lower.find(L"mega.nz/") != std::wstring::npos || lower.rfind(L"mega:", 0) == 0) {
        std::wstring handle, key;
        bool folder = false;
        if (ParseMegaLink(url, handle, key, folder)) return folder ? Provider::MegaFolder : Provider::Mega;
        return Provider::Mega;
    }
    if (lower.find(L"drive.google.com/") != std::wstring::npos ||
        lower.find(L"drive.usercontent.google.com/") != std::wstring::npos ||
        lower.find(L"docs.google.com/") != std::wstring::npos)
        return Provider::GoogleDrive;
    if (lower.find(L"mediafire.com/") != std::wstring::npos) return Provider::MediaFire;
    return Provider::Direct;
}

bool IsMegaLink(const std::wstring& url) {
    const Provider provider = DetectProvider(url);
    return provider == Provider::Mega || provider == Provider::MegaFolder;
}

bool IsGoogleDriveLink(const std::wstring& url) {
    return DetectProvider(url) == Provider::GoogleDrive;
}

bool IsMediaFireLink(const std::wstring& url) {
    return DetectProvider(url) == Provider::MediaFire;
}

std::wstring NormalizeDriveUrl(const std::wstring& url) {
    if (DetectProvider(url) != Provider::GoogleDrive) return {};
    std::wstring id = DriveFileId(url);
    if (id.empty()) return {};
    std::wstring direct = L"https://drive.usercontent.google.com/download?id=" + id +
                          L"&export=download&confirm=t";
    const std::wstring resourceKey = QueryParam(url, L"resourcekey");
    if (!resourceKey.empty()) direct += L"&resourcekey=" + resourceKey;
    return direct;
}

bool MegaCtrXor(const std::vector<unsigned char>& fileKey, uint64_t offset,
                unsigned char* data, size_t length) {
    if (fileKey.size() < 32) return false;
    // MEGA derives the data key by XORing the two 16-byte halves of the file
    // key. The CTR nonce is words 4 and 5 of the key followed by zeroes.
    std::vector<unsigned char> key(fileKey.begin(), fileKey.begin() + 16);
    for (size_t i = 0; i < 16; ++i) key[i] ^= fileKey[i + 16];
    unsigned char nonce[16] = {};
    std::memcpy(nonce, fileKey.data() + 16, 8);
    return AesCtrXor(key, nonce, offset, data, length);
}

ResolvedLink ResolveLink(const std::wstring& url, const std::wstring& suppliedKey) {    const std::wstring trimmed = util::Trim(url);
    switch (DetectProvider(trimmed)) {
    case Provider::GoogleDrive:
        return ResolveDrive(trimmed);
    case Provider::MediaFire:
        return ResolveMediaFire(trimmed);
    case Provider::Mega:
    case Provider::MegaFolder:
        return ResolveMega(trimmed, suppliedKey);
    case Provider::Direct:
    default:
        break;
    }
    ResolvedLink direct;
    direct.provider = Provider::Direct;
    direct.url = trimmed;
    if (trimmed.rfind(L"magnet:", 0) != 0 && trimmed.find(L"://") == std::wstring::npos &&
        !util::FileExists(trimmed)) {
        direct.error = L"That does not look like a download link.";
    }
    return direct;
}
