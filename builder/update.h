// Self-update from the project's GitHub releases.
#pragma once
#include "util.h"
#include <optional>

namespace hb {
struct UpdateInfo {
    std::string version;          // "1.0.1"
    std::string url, sha_url;     // release asset for this platform and its .sha256 file
    uint64_t bytes = 0;
};
const char* builder_version();                     // "1.0.0", "0.0.0" for development builds
std::optional<UpdateInfo> check_for_update();      // never throws; nullopt when current or offline
// Downloads and verifies the new builder; emits "Download N%". Throws UserError.
fs::path download_update(const UpdateInfo& info, const Emit& emit);
// Starts the new builder (Windows: in place of this exe; macOS: from the mounted new disk image).
// The caller must quit right after. Throws UserError.
void apply_update(const fs::path& file);
// In the new builder: after the old one exits, eject its disk image (macOS) or delete its file (Windows).
// Returns the version it replaced, or empty.
std::string finish_update(int argc, char** argv);
}
