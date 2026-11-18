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
}
