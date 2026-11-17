#pragma once
#include "util.h"

namespace hb {
struct ZipEntry { std::string name; uint16_t method = 0; uint32_t compressed = 0, size = 0, external = 0, local = 0; };
std::vector<ZipEntry> zip_entries(const Bytes& archive);
Bytes zip_read(const Bytes& archive, const ZipEntry& entry);
// Rejects absolute, parent, drive, backslash, symlink and case-duplicate members, and archives over 4 GB.
void zip_extract(const Bytes& archive, const fs::path& target);
void zip_extract_file(const fs::path& archive, const fs::path& target);
}
