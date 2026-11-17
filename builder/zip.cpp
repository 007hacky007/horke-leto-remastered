// Read-only ZIP extraction with the same safety rules as the Python builder.
#include "zip.h"
#include <algorithm>
#include <cstring>
#include <set>

extern "C" char* stbi_zlib_decode_noheader_malloc(const char* buffer, int len, int* outlen);

namespace hb {
static uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
static uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

std::vector<ZipEntry> zip_entries(const Bytes& data) {
    if (data.size() < 22) throw UserError("Archiv ZIP je poškozený.");
    size_t end = data.size() - 22;
    while (end > 0 && u32(&data[end]) != 0x06054b50) --end;
    if (u32(&data[end]) != 0x06054b50) throw UserError("Archiv ZIP je poškozený.");
    size_t count = u16(&data[end + 10]), at = u32(&data[end + 16]);
    std::vector<ZipEntry> out;
    for (size_t i = 0; i < count; ++i) {
        if (at + 46 > data.size() || u32(&data[at]) != 0x02014b50) throw UserError("Archiv ZIP je poškozený.");
        ZipEntry e;
        e.method = u16(&data[at + 10]);
        e.compressed = u32(&data[at + 20]);
        e.size = u32(&data[at + 24]);
        size_t name = u16(&data[at + 28]), extra = u16(&data[at + 30]), comment = u16(&data[at + 32]);
        e.external = u32(&data[at + 38]);
        e.local = u32(&data[at + 42]);
        e.name.assign(reinterpret_cast<const char*>(&data[at + 46]), name);
        out.push_back(e);
        at += 46 + name + extra + comment;
    }
    return out;
}

Bytes zip_read(const Bytes& data, const ZipEntry& e) {
    if (e.local + 30 > data.size() || u32(&data[e.local]) != 0x04034b50) throw UserError("Archiv ZIP je poškozený.");
    size_t start = e.local + 30 + u16(&data[e.local + 26]) + u16(&data[e.local + 28]);
    if (start + e.compressed > data.size()) throw UserError("Archiv ZIP je poškozený.");
    if (e.method == 0) return Bytes(data.begin() + start, data.begin() + start + e.size);
    if (e.method != 8) throw UserError("Archiv ZIP používá nepodporovanou kompresi.");
    int size = 0;
    char* plain = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(&data[start]), int(e.compressed), &size);
    if (!plain || size_t(size) != e.size) { free(plain); throw UserError("Archiv ZIP je poškozený."); }
    Bytes out(plain, plain + size);
    free(plain);
    return out;
}

void zip_extract(const Bytes& data, const fs::path& target) {
    auto entries = zip_entries(data);
    uint64_t total = 0;
    std::set<std::string> seen;
    for (auto& e : entries) {
        total += e.size;
        std::string lower = e.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        bool link = ((e.external >> 16) & 0170000) == 0120000;
        if (e.name.empty() || e.name[0] == '/' || e.name.find('\\') != std::string::npos ||
            e.name.find(':') != std::string::npos || e.name.find("..") != std::string::npos || link ||
            !seen.insert(lower).second)
            throw UserError("Archiv obsahuje nebezpečnou cestu: " + e.name);
    }
    if (total > 4ull << 30) throw UserError("Archiv je příliš velký.");
    fs::create_directories(target);
    for (auto& e : entries) {
        auto path = target / fs::u8path(e.name);
        if (e.name.back() == '/') { fs::create_directories(path); continue; }
        fs::create_directories(path.parent_path());
        auto bytes = zip_read(data, e);
        write_file(path, bytes.data(), bytes.size());
        if ((e.external >> 16) & 0111)
            fs::permissions(path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add);
    }
}

void zip_extract_file(const fs::path& archive, const fs::path& target) { zip_extract(read_file(archive), target); }
}
