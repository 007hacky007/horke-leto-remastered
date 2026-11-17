// Checks shown before the build starts, in the 1990s DOS box.
#pragma once
#include "util.h"

namespace hb {
struct Check {
    std::string key, title;
    enum State { Ok, Warn, Fail } state = Ok;
    std::string text, hint;
    std::string fix;  // "archive" or "output" when a button can fix it
};
// Runs every check in order, calling `row` as each finishes.
void run_preflight(const fs::path& image, const std::string& drive, const fs::path& output, const fs::path& archive,
                   const std::function<void(const Check&)>& row);
}
