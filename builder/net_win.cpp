// HTTPS download through the system's WinHTTP (no bundled TLS library).
#include "pipeline.h"
#include <windows.h>
#include <winhttp.h>
#include <fstream>

namespace hb {
static std::wstring wide(const std::string& s) {
    std::wstring out(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), int(out.size()));
    out.pop_back();
    return out;
}

void download(const std::string& url, const fs::path& target, const std::string& sha256, const Emit& emit, const std::string& message) {
    if (fs::is_regular_file(target) && sha_file(target) == sha256) { emit("Download 100%"); return; }
    auto fail = [&] { return UserError(message); };
    auto address = wide(url);
    URL_COMPONENTS parts{sizeof parts};
    wchar_t host[256], path[2048];
    parts.lpszHostName = host; parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path; parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts)) throw fail();
    HINTERNET session = WinHttpOpen(L"HorkeLetoBuilder/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, nullptr, nullptr,
                                                         parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    struct Close { HINTERNET a, b, c; ~Close() { if (c) WinHttpCloseHandle(c); if (b) WinHttpCloseHandle(b); if (a) WinHttpCloseHandle(a); } } close{session, connection, request};
    if (!request || !WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) throw fail();
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr);
    if (status != 200) throw fail();
    wchar_t length_text[32]{};
    DWORD length_size = sizeof length_text;
    uint64_t total = 0, got = 0;
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, length_text, &length_size, nullptr)) total = _wtoi64(length_text);
    fs::create_directories(target.parent_path());
    fs::path partial = target.string() + ".partial";
    {
        std::ofstream out(partial, std::ios::binary);
        std::vector<char> buffer(1 << 20);
        int last = -1;
        for (DWORD read = 0;; ) {
            check_cancelled();
            if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &read)) throw fail();
            if (!read) break;
            out.write(buffer.data(), read);
            got += read;
            int percent = total ? int(got * 100 / total) : -1;
            if (percent >= 0 && percent != last) { emit("Download " + std::to_string(percent) + "%"); last = percent; }
        }
    }
    if (sha_file(partial) != sha256) { fs::remove(partial); throw UserError("Stažený balíček má jiný kontrolní součet; zkus to znovu."); }
    fs::rename(partial, target);
    emit("Download 100%");
}

std::string http_get(const std::string& url) {
    auto fail = [] { return UserError("Server neodpověděl."); };
    auto address = wide(url);
    URL_COMPONENTS parts{sizeof parts};
    wchar_t host[256], path[2048];
    parts.lpszHostName = host; parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path; parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts)) throw fail();
    HINTERNET session = WinHttpOpen(L"HorkeLetoBuilder", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (session) WinHttpSetTimeouts(session, 15000, 15000, 15000, 15000);
    HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, nullptr, nullptr, nullptr,
                                                         parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    struct Close { HINTERNET a, b, c; ~Close() { if (c) WinHttpCloseHandle(c); if (b) WinHttpCloseHandle(b); if (a) WinHttpCloseHandle(a); } } close{session, connection, request};
    const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
    if (!request || !WinHttpSendRequest(request, headers, DWORD(-1), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(request, nullptr)) throw fail();
    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr);
    if (status != 200) throw fail();
    std::string body;
    std::vector<char> buffer(1 << 16);
    for (DWORD read = 0;;) {
        if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &read)) throw fail();
        if (!read) break;
        body.append(buffer.data(), read);
        if (body.size() > (8u << 20)) throw fail();
    }
    return body;
}

uint64_t available_bytes(const fs::path& path) {
    std::error_code ec;
    return fs::space(path, ec).available;
}

bool url_reachable(const std::string& url) {
    auto address = wide(url);
    URL_COMPONENTS parts{sizeof parts};
    wchar_t host[256], path[2048];
    parts.lpszHostName = host; parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path; parts.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts)) return false;
    HINTERNET session = WinHttpOpen(L"HorkeLetoBuilder/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
    if (session) WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);
    HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : nullptr;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"HEAD", path, nullptr, nullptr, nullptr,
                                                         parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    DWORD status = 0, size = sizeof status;
    bool ok = request && WinHttpSendRequest(request, nullptr, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr) &&
              WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &size, nullptr) && status == 200;
    for (HINTERNET h : {request, connection, session}) if (h) WinHttpCloseHandle(h);
    return ok;
}

KeepAwake::KeepAwake() { SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED); }
KeepAwake::~KeepAwake() { SetThreadExecutionState(ES_CONTINUOUS); }
}
