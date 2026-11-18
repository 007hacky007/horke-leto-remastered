// The whole build: verified source -> data -> frames -> art -> Original -> Remastered -> playable copy.
#pragma once
#include "util.h"

namespace hb {
struct BuildRequest {
    fs::path image;        // CD image, or empty when reading a drive
    std::string drive;     // Windows drive letter such as "D:"
    fs::path output;       // the playable copy goes here; work files in output/.builder
    fs::path art_archive;  // optional local copy of the pinned art release
    bool cleanup = true;   // remove output/.builder after success
};

json builder_pins();                    // pins.json embedded in the builder
fs::path payload_dir(const Emit& emit); // extracts the embedded payload once, returns its folder
const char* app_name();                 // "Horke Leto.app" or "HorkeLeto"
fs::path build_game(const BuildRequest& request, const Emit& emit);

// Downloads with SHA-256 verification; progress lines "Download N%".
void download(const std::string& url, const fs::path& target, const std::string& sha256, const Emit& emit,
              const std::string& failure = "Výtvarný balíček nejde stáhnout. Zkontroluj připojení, nebo vyber stažený archiv.");
// GET into memory (GitHub API, checksum files). Throws UserError on failure or a non-200 answer.
std::string http_get(const std::string& url);
// macOS: where a Gatekeeper-translocated app really lives (its disk image); the input elsewhere.
fs::path original_bundle_path(const fs::path& bundle);
// Free bytes for new files, including what the OS can purge on demand (macOS snapshots).
uint64_t available_bytes(const fs::path& path);
// True when a HEAD request to the URL answers 200 (after redirects).
bool url_reachable(const std::string& url);
// Keep the computer awake (and the Mac app out of App Nap) while an object lives.
struct KeepAwake { KeepAwake(); ~KeepAwake(); KeepAwake(const KeepAwake&) = delete; void* token = nullptr; };
// Checks the embedded payload and pins; returns a short report or throws.
std::string self_check();

// Game icon from the player's data: writes icon files and returns the RGBA 1024 image path.
fs::path make_game_icon(const fs::path& work, const fs::path& app);
}
