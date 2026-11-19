#include "util.h"
#include "data/hash.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <span>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace hb {
std::atomic<bool> cancelled{false};

bool selected_resource(const std::string& name) {
    const char* only = std::getenv("HB_ONLY");
    if (!only || !*only) return true;
    std::string list = std::string(",") + only + ",";
    return list.find("," + name + ",") != std::string::npos;
}

void check_cancelled() {
    if (cancelled) throw UserError("Sestavení bylo přerušeno.");
}

std::string sha_bytes(const void* data, size_t size) {
    return hl::sha256(std::as_bytes(std::span(static_cast<const uint8_t*>(data), size)));
}

std::string sha_file(const fs::path& path) {
    auto digest = hl::sha256_file(path);
    if (!digest.has_value()) throw std::runtime_error("Nelze přečíst " + path.string());
    return digest.value();
}

Bytes read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Nelze otevřít " + path.string());
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

void write_file(const fs::path& path, const void* data, size_t size) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    auto temporary = path; temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary);
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!out) throw std::runtime_error("Nelze zapsat " + path.string());
    }
    fs::rename(temporary, path);
}

json read_json(const fs::path& path) {
    auto bytes = read_file(path);
    return json::parse(bytes.begin(), bytes.end());
}

void write_json(const fs::path& path, const json& value) {
    auto text = value.dump(2, ' ', true) + "\n";
    write_file(path, text.data(), text.size());
}

bool Checkpoint::valid() const {
    try {
        auto value = read_json(path_);
        if (value.at("key").get<std::string>() != key_ || value.at("files").empty()) return false;
        for (auto& f : value.at("files")) {
            fs::path p = f.at("path").get<std::string>();
            if (!fs::is_regular_file(p) || sha_file(p) != f.at("sha256").get<std::string>()) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

void Checkpoint::commit(const std::vector<fs::path>& files) const {
    json list = json::array();
    for (auto& p : files) list.push_back({{"path", p.string()}, {"sha256", sha_file(p)}});
    write_json(path_, {{"key", key_}, {"files", list}});
}

std::vector<fs::path> files_under(const fs::path& root) {
    std::vector<fs::path> out;
    for (auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

void remove_tree(const fs::path& path) {
    std::error_code ec;
    fs::remove_all(path, ec);
}

#ifdef _WIN32
namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// Quote one argument the way CommandLineToArgvW and the C runtime split it back.
void append_quoted(std::wstring& line, const std::wstring& arg) {
    if (!line.empty()) line += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) { line += arg; return; }
    line += L'"';
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        line.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        line += c;
    }
    line.append(slashes * 2, L'\\');
    line += L'"';
}
}

// Windows: start console tools (upscaler, hl-tool) without a console window of their own.
// SDL can only hide it in "background" mode, which throws the exit code away.
int run_process(const std::vector<std::string>& argv, const fs::path& log) {
    std::wstring line;
    for (auto& a : argv) append_quoted(line, widen(a));
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE null_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    HANDLE read_end = nullptr, write_end = nullptr;
    if (log.empty()) {
        write_end = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    } else if (CreatePipe(&read_end, &write_end, &inherit, 0)) {
        SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    }
    auto close = [](HANDLE& h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; };
    if (null_in == INVALID_HANDLE_VALUE || !write_end || write_end == INVALID_HANDLE_VALUE) {
        close(null_in); close(read_end); close(write_end);
        throw std::runtime_error("Nelze spustit " + argv[0] + ": nelze připravit vstup a výstup");
    }
    // Hand the child only these two handles, so parallel runs do not keep each other's pipes open.
    HANDLE handles[2] = {null_in, write_end};
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> attributes(size);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    bool listed = InitializeProcThreadAttributeList(list, 1, 0, &size) &&
                  UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = null_in;
    startup.StartupInfo.hStdOutput = write_end;
    startup.StartupInfo.hStdError = write_end;
    startup.lpAttributeList = listed ? list : nullptr;
    PROCESS_INFORMATION info{};
    DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | (listed ? EXTENDED_STARTUPINFO_PRESENT : 0);
    BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr,
                                  &startup.StartupInfo, &info);
    DWORD error = GetLastError();
    if (listed) DeleteProcThreadAttributeList(list);
    close(null_in); close(write_end);
    if (!started) {
        close(read_end);
        throw std::runtime_error("Nelze spustit " + argv[0] + ": chyba " + std::to_string(error));
    }
    CloseHandle(info.hThread);
    if (read_end) {
        std::string output;
        char buffer[65536];
        DWORD got = 0;
        while (ReadFile(read_end, buffer, sizeof(buffer), &got, nullptr) && got > 0) output.append(buffer, got);
        close(read_end);
        write_file(log, output.data(), output.size());
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exit = 0;
    GetExitCodeProcess(info.hProcess, &exit);
    CloseHandle(info.hProcess);
    return int(exit);
}
#else
int run_process(const std::vector<std::string>& argv, const fs::path& log) {
    std::vector<const char*> args;
    for (auto& a : argv) args.push_back(a.c_str());
    args.push_back(nullptr);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, log.empty() ? SDL_PROCESS_STDIO_NULL : SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!process) throw std::runtime_error(std::string("Nelze spustit ") + argv[0] + ": " + SDL_GetError());
    int exit = -1;
    if (!log.empty()) {
        size_t size = 0;
        void* output = SDL_ReadProcess(process, &size, &exit);
        write_file(log, output ? output : "", output ? size : 0);
        SDL_free(output);
    } else {
        SDL_WaitProcess(process, true, &exit);
    }
    SDL_DestroyProcess(process);
    return exit;
}
#endif

fs::path executable_path() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    auto n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(n);
    return fs::path(buffer);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    _NSGetExecutablePath(buffer.data(), &size);
    return fs::canonical(buffer.c_str());
#else
    return fs::canonical("/proc/self/exe");
#endif
}
}
