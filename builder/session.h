// The unfinished build remembered between runs, so the first screen can offer to resume it.
#pragma once
#include "util.h"
#include <optional>

namespace hb {
struct Session {
    std::string image, drive, output, archive;  // image or drive is set
    bool cleanup = true;
    int percent = 0, stage = 0;
    int64_t started = 0, updated = 0;           // seconds since the epoch
};
std::optional<Session> load_session();          // only when its .builder folder still exists
void save_session(const Session& session);
void clear_session();
// Every game this builder finished or updated, for "Aktualizovat hru" on the next start.
void remember_game(const fs::path& app);
// Remembered games plus the usual places (default target, Downloads, Applications, Desktop) that exist.
std::vector<fs::path> known_games();
}
