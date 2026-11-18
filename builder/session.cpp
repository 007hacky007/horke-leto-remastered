#include "session.h"
#include <SDL3/SDL.h>

namespace hb {
static fs::path session_file() {
    if (const char* dir = SDL_getenv("HB_SESSION_DIR")) return fs::u8path(dir) / "session.json";  // tests
    char* pref = SDL_GetPrefPath("hl-port", "HorkeLetoBuilder");
    if (!pref) return {};
    fs::path path = fs::u8path(pref) / "session.json";
    SDL_free(pref);
    return path;
}

std::optional<Session> load_session() {
    auto path = session_file();
    try {
        if (path.empty() || !fs::exists(path)) return std::nullopt;
        auto j = read_json(path);
        Session s;
        s.image = j.value("image", ""); s.drive = j.value("drive", ""); s.output = j.at("output");
        s.archive = j.value("archive", ""); s.cleanup = j.value("cleanup", true);
        s.percent = j.value("percent", 0); s.stage = j.value("stage", 0);
        s.started = j.value("started", int64_t(0)); s.updated = j.value("updated", int64_t(0));
        if (s.output.empty() || (s.image.empty() && s.drive.empty())) return std::nullopt;
        // Nothing to resume once the work folder is gone (finished, cleaned up or deleted by hand).
        if (!fs::is_directory(fs::u8path(s.output) / ".builder")) { clear_session(); return std::nullopt; }
        return s;
    } catch (...) {
        return std::nullopt;
    }
}

void save_session(const Session& s) {
    auto path = session_file();
    if (path.empty()) return;
    try {
        fs::create_directories(path.parent_path());
        write_json(path, {{"schema_version", 1}, {"image", s.image}, {"drive", s.drive}, {"output", s.output},
                          {"archive", s.archive}, {"cleanup", s.cleanup}, {"percent", s.percent}, {"stage", s.stage},
                          {"started", s.started}, {"updated", s.updated}});
    } catch (...) {}
}

static fs::path games_file() { auto s = session_file(); return s.empty() ? s : s.parent_path() / "games.json"; }

void remember_game(const fs::path& app) {
    auto path = games_file();
    if (path.empty()) return;
    try {
        json list = fs::exists(path) ? read_json(path) : json{{"schema_version", 1}, {"games", json::array()}};
        auto text = app.string();
        for (auto& g : list.at("games")) if (g == text) return;
        list["games"].push_back(text);
        fs::create_directories(path.parent_path());
        write_json(path, list);
    } catch (...) {}
}

std::vector<fs::path> known_games() {
    std::vector<fs::path> out;
    auto add = [&](const fs::path& p) {
        std::error_code ec;
        if (p.empty() || !fs::exists(p, ec)) return;
        for (auto& q : out) if (fs::equivalent(q, p, ec)) return;
        out.push_back(p);
    };
    try {
        auto path = games_file();
        if (!path.empty() && fs::exists(path))
            for (auto& g : read_json(path).at("games")) add(fs::u8path(g.get<std::string>()));
    } catch (...) {}
#ifdef _WIN32
    const char* name = "HorkeLeto";
#else
    const char* name = "Horke Leto.app";
#endif
    if (const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME)) add(fs::u8path(home) / "Games" / "Horke Leto" / name);
    for (auto folder : {SDL_FOLDER_DOWNLOADS, SDL_FOLDER_DESKTOP})
        if (const char* d = SDL_GetUserFolder(folder)) { add(fs::u8path(d) / name); add(fs::u8path(d) / "Horke Leto" / name); }
#ifndef _WIN32
    add("/Applications/Horke Leto.app");
    if (const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME)) add(fs::u8path(home) / "Applications" / name);
#endif
    return out;
}

void clear_session() {
    auto path = session_file();
    std::error_code ec;
    if (!path.empty()) fs::remove(path, ec);
}
}
