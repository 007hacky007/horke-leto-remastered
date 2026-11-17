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
