// Turn a CD image (BIN/CUE, IMG/CCD, MDF/MDS, NRG) or a Windows CD drive into the verified BIN/CUE.
#pragma once
#include "util.h"

namespace hb {
struct DiscPins {
    std::vector<std::string> track_layout;
    uint64_t volume_sectors = 0, data_sectors = 0, audio_sectors = 0;
    std::string key_file, key_sha256;  // one game file checked before the build; all files are checked after import
};
DiscPins disc_pins(const json& pins);

std::string detect_format(const fs::path& path);  // cue, ccd, mds, nrg, iso, chd; throws UserError
std::string format_name(const std::string& id);
// Check without writing; returns the format id.
std::string identify_disc(const fs::path& source, const DiscPins& pins, const Emit& emit);
// Write work/disc.bin (+ .cue) in the pinned track layout; returns the CUE path.
fs::path normalize_disc(const fs::path& source, const fs::path& work, const DiscPins& pins, const Emit& emit);

struct Drive { std::string letter, label; };
std::vector<Drive> optical_drives();  // empty outside Windows
// Rip the whole disc into work/disc.bin; game files are verified by the caller.
fs::path rip_drive(const std::string& letter, const fs::path& work, const DiscPins& pins, const Emit& emit);
}
