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

void clear_session() {
    auto path = session_file();
    std::error_code ec;
    if (!path.empty()) fs::remove(path, ec);
}
}
