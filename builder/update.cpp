#include "update.h"
#include "pipeline.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

#ifndef HB_VERSION
#define HB_VERSION "0.0.0"
#endif

namespace hb {
const char* builder_version() {
    static std::string v = [] { std::string s = HB_VERSION; return s.rfind("v", 0) == 0 ? s.substr(1) : s; }();
    return v.c_str();
}

static std::vector<int> parts(std::string v) {
    if (v.rfind("v", 0) == 0) v = v.substr(1);
    std::vector<int> out;
    size_t at = 0;
    while (at < v.size() && out.size() < 4) {
        size_t end = v.find('.', at);
        try { out.push_back(std::stoi(v.substr(at, end - at))); } catch (...) { out.push_back(0); }
        if (end == std::string::npos) break;
        at = end + 1;
    }
    while (out.size() < 3) out.push_back(0);
    return out;
}

std::optional<UpdateInfo> check_for_update() {
    try {
        auto pins = builder_pins();
        const char* override_api = SDL_getenv("HB_UPDATE_API");  // tests: a file:// or local server URL
        if (std::string(builder_version()) == "0.0.0" && !override_api) return std::nullopt;
        auto release = json::parse(http_get(override_api ? override_api : pins.at("updates").at("api").get<std::string>()));
        if (release.value("draft", false) || release.value("prerelease", false)) return std::nullopt;
        std::string tag = release.at("tag_name");
        if (parts(tag) <= parts(builder_version())) return std::nullopt;
#ifdef _WIN32
        std::string asset = pins.at("updates").at("windows_asset");
#else
        std::string asset = pins.at("updates").at("macos_asset");
#endif
        UpdateInfo info;
        info.version = tag.rfind("v", 0) == 0 ? tag.substr(1) : tag;
        for (auto& a : release.at("assets")) {
            if (a.at("name") == asset) { info.url = a.at("browser_download_url"); info.bytes = a.value("size", uint64_t(0)); }
            if (a.at("name") == asset + ".sha256") info.sha_url = a.at("browser_download_url");
        }
        if (info.url.empty() || info.sha_url.empty()) return std::nullopt;
        return info;
    } catch (...) {
        return std::nullopt;
    }
}

static fs::path download_folder() {
#ifdef _WIN32
    // Next to the running builder, so the new one can take its place; temp when that folder is read-only.
    auto folder = executable_path().parent_path();
    std::error_code ec;
    auto probe = folder / ".hb-write-test";
    { std::ofstream test(probe); if (test) { test.close(); fs::remove(probe, ec); return folder; } }
    return fs::temp_directory_path();
#else
    if (const char* d = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)) return fs::u8path(d);
    return fs::temp_directory_path();
#endif
}

fs::path download_update(const UpdateInfo& info, const Emit& emit) {
    auto text = http_get(info.sha_url);
    auto sha = text.substr(0, std::min<size_t>(64, text.size()));
    if (sha.size() != 64 || sha.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw UserError("Kontrolní součet nové verze se nepodařilo načíst.");
    auto name = fs::u8path(info.url.substr(info.url.find_last_of('/') + 1));
    auto target = download_folder() / (name.stem().string() + "-" + info.version + name.extension().string());
    download(info.url, target, sha, emit, "Novou verzi se nepodařilo stáhnout. Zkontroluj připojení a zkus to znovu.");
    return target;
}

void apply_update(const fs::path& file) {
    auto exe = executable_path();
#ifdef _WIN32
    std::string pid = std::to_string(GetCurrentProcessId());
    fs::path launch = file, old;
    // A running exe can be renamed but not overwritten: move ourselves aside, put the new one in our place.
    if (file.parent_path() == exe.parent_path()) {
        old = exe; old += ".old";
        std::error_code ec;
        fs::remove(old, ec);
        if (MoveFileExW(exe.wstring().c_str(), old.wstring().c_str(), MOVEFILE_REPLACE_EXISTING) &&
            MoveFileExW(file.wstring().c_str(), exe.wstring().c_str(), MOVEFILE_REPLACE_EXISTING)) launch = exe;
        else { MoveFileExW(old.wstring().c_str(), exe.wstring().c_str(), 0); old.clear(); }
    }
    std::vector<std::string> argv{launch.string(), "--after-update", pid, old.string(), builder_version()};
    std::vector<const char*> args;
    for (auto& a : argv) args.push_back(a.c_str());
    args.push_back(nullptr);
    SDL_Process* p = SDL_CreateProcess(args.data(), false);
    if (!p) throw UserError("Novou verzi se nepodařilo spustit.");
    SDL_DestroyProcess(p);
#else
    auto log = fs::temp_directory_path() / "hb-update-attach.log";
    if (run_process({"/usr/bin/hdiutil", "attach", "-noautoopen", file.string()}, log) != 0)
        throw UserError("Obraz disku s novou verzí se nepodařilo připojit.");
    std::string out;
    { auto bytes = read_file(log); out.assign(bytes.begin(), bytes.end()); }
    auto at = out.rfind("/Volumes/");
    if (at == std::string::npos) throw UserError("Obraz disku s novou verzí se nepodařilo připojit.");
    fs::path volume = out.substr(at, out.find('\n', at) - at);
    fs::path app;
    for (auto& e : fs::directory_iterator(volume)) if (e.path().extension() == ".app") app = e.path();
    if (app.empty()) throw UserError("Na obrazu disku chybí průvodce.");
    // Our own disk image is ejected by the new builder once we have quit.
    auto bundle = original_bundle_path(exe.parent_path().parent_path().parent_path());
    std::string old_volume;
    auto s = bundle.string();
    if (s.rfind("/Volumes/", 0) == 0) old_volume = s.substr(0, s.find('/', 9));
    if (old_volume == volume.string()) old_volume.clear();
    std::string pid = std::to_string(getpid());
    if (run_process({"/usr/bin/open", "-n", app.string(), "--args", "--after-update", pid, old_volume, builder_version()}) != 0)
        throw UserError("Novou verzi se nepodařilo spustit.");
#endif
}

std::string finish_update(int argc, char** argv) {
    for (int i = 1; i + 3 < argc; ++i) {
        if (std::string(argv[i]) != "--after-update") continue;
        long pid = std::atol(argv[i + 1]);
        std::string leftover = argv[i + 2], replaced = argv[i + 3];
        std::thread([pid, leftover] {
#ifdef _WIN32
            if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid))) { WaitForSingleObject(h, 20000); CloseHandle(h); }
            for (int attempt = 0; attempt < 20 && !leftover.empty(); ++attempt) {
                std::error_code ec;
                if (fs::remove(fs::u8path(leftover), ec) || !fs::exists(fs::u8path(leftover))) break;
                SDL_Delay(500);
            }
#else
            for (int t = 0; t < 200 && pid > 0 && kill(pid_t(pid), 0) == 0; ++t) SDL_Delay(100);
            if (!leftover.empty())
                for (int attempt = 0; attempt < 5; ++attempt) {
                    if (run_process({"/usr/bin/hdiutil", "detach", leftover, "-quiet"}) == 0) break;
                    SDL_Delay(1500);
                }
#endif
        }).detach();
        return replaced;
    }
    return {};
}
}
