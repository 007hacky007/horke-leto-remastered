#include "disc.h"
#include "data/hash.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <regex>
#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <ntddcdrm.h>
#endif

namespace hb {
constexpr uint64_t SECTOR = 2352, SUB_SECTOR = 2448, PREGAP = 150;

DiscPins disc_pins(const json& pins) {
    auto& d = pins.at("disc");
    DiscPins out;
    out.track_layout = d.at("track_layout").get<std::vector<std::string>>();
    out.volume_sectors = d.at("volume_sectors"); out.data_sectors = d.at("data_sectors"); out.audio_sectors = d.at("audio_sectors");
    out.key_file = d.at("key_file");
    out.key_sha256 = pins.at("files").at(out.key_file);
    return out;
}

static std::string lower_ext(const fs::path& p) {
    auto e = p.extension().string();
    if (!e.empty()) e.erase(0, 1);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e;
}

static int msf(const std::string& text) {
    int m = 0, s = 0, f = 0;
    std::sscanf(text.c_str(), "%d:%d:%d", &m, &s, &f);
    return (m * 60 + s) * 75 + f;
}

std::string format_name(const std::string& id) {
    static const std::map<std::string, std::string> names{{"cue", "BIN/CUE"}, {"iso", "ISO"}, {"ccd", "IMG/CCD"},
        {"mds", "MDF/MDS"}, {"nrg", "NRG"}, {"chd", "CHD"}, {"drive", "CD mechanika"}};
    auto it = names.find(id);
    return it == names.end() ? id : it->second;
}

std::string detect_format(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw UserError("Soubor nejde otevřít.");
    char head[16]{}, tail[12]{};
    in.read(head, 16);
    auto size = fs::file_size(path);
    if (size >= 12) { in.clear(); in.seekg(std::streamoff(size - 12)); in.read(tail, 12); }
    if (!std::memcmp(head, "MComprHD", 8)) return "chd";
    if (!std::memcmp(tail + 4, "NERO", 4) || !std::memcmp(tail, "NER5", 4)) return "nrg";
    if (!std::memcmp(head, "MEDIA DESCRIPTOR", 16)) return "mds";
    auto ext = lower_ext(path);
    if (ext == "cue" || ext == "bin") return "cue";
    if (ext == "ccd" || ext == "img" || ext == "sub") return "ccd";
    if (ext == "mdf" || ext == "mds") return "mds";
    if (ext == "iso" || ext == "nrg" || ext == "chd") return ext;
    throw UserError("Tenhle soubor nevypadá jako obraz CD. Vyber BIN/CUE, IMG/CCD, MDF/MDS, NRG nebo ISO.");
}

static fs::path sibling(const fs::path& p, std::initializer_list<const char*> exts) {
    auto stem = p.stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(), ::tolower);
    for (auto& e : fs::directory_iterator(p.parent_path())) {
        auto s = e.path().stem().string();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s != stem) continue;
        for (auto x : exts) if (lower_ext(e.path()) == x) return e.path();
    }
    return {};
}

struct Piece { fs::path path; uint64_t offset, count; };
struct Stream {
    std::vector<Piece> pieces; uint64_t stride = SECTOR;
    uint64_t sectors() const { uint64_t n = 0; for (auto& p : pieces) n += p.count; return n; }
    // Calls sink with raw 2352-byte sectors, dropping [skip_start, skip_start+skip_count).
    // One raw 2352-byte sector.
    Bytes raw(uint64_t index) const {
        for (auto& p : pieces) {
            if (index >= p.count) { index -= p.count; continue; }
            std::ifstream in(p.path, std::ios::binary);
            in.seekg(std::streamoff(p.offset + index * stride));
            Bytes out(SECTOR);
            in.read(reinterpret_cast<char*>(out.data()), SECTOR);
            return out;
        }
        throw UserError("Obraz CD je zkrácený nebo poškozený.");
    }
    // The 2048 user bytes of a Mode 1 sector.
    Bytes user_data(uint64_t index) const {
        for (auto& p : pieces) {
            if (index >= p.count) { index -= p.count; continue; }
            std::ifstream in(p.path, std::ios::binary);
            in.seekg(std::streamoff(p.offset + index * stride + 16));
            Bytes out(2048);
            in.read(reinterpret_cast<char*>(out.data()), 2048);
            if (in.gcount() != 2048) throw UserError("Obraz CD je zkrácený nebo poškozený.");
            return out;
        }
        throw UserError("Obraz CD je zkrácený nebo poškozený.");
    }
    template <class F> void read(uint64_t skip_start, uint64_t skip_count, F sink) const {
        std::vector<char> buffer(2048 * stride);
        uint64_t index = 0;
        for (auto& p : pieces) {
            std::ifstream in(p.path, std::ios::binary);
            in.seekg(std::streamoff(p.offset));
            for (uint64_t left = p.count; left;) {
                uint64_t n = std::min<uint64_t>(2048, left);
                in.read(buffer.data(), std::streamsize(n * stride));
                if (uint64_t(in.gcount()) != n * stride) throw UserError("Obraz CD je zkrácený nebo poškozený.");
                for (uint64_t k = 0; k < n; ++k, ++index)
                    if (!(skip_count && index >= skip_start && index < skip_start + skip_count))
                        sink(reinterpret_cast<const uint8_t*>(buffer.data() + k * stride));
                left -= n;
                check_cancelled();
            }
        }
    }
};

static std::vector<Piece> cue_pieces(const fs::path& cue) {
    auto bytes = read_file(cue);
    std::string text(bytes.begin(), bytes.end());
    std::regex line(R"re(^\s*FILE\s+"?(.+?)"?\s+(BINARY|MOTOROLA|WAVE)\s*$)re", std::regex::icase | std::regex::multiline);
    std::vector<Piece> out;
    for (std::sregex_iterator it(text.begin(), text.end(), line), end; it != end; ++it) {
        std::string kind = (*it)[2];
        std::transform(kind.begin(), kind.end(), kind.begin(), ::toupper);
        if (kind != "BINARY") throw UserError("CUE se zvukem ve WAV nepodporujeme. Použij BIN/CUE, kde jsou stopy jako BIN.");
        std::string name = (*it)[1];
        std::replace(name.begin(), name.end(), '\\', '/');
        auto path = cue.parent_path() / fs::u8path(name).filename();
        if (!fs::is_regular_file(path)) throw UserError("Chybí soubor " + path.filename().string() + " vedle CUE.");
        auto size = fs::file_size(path);
        if (size % SECTOR) throw UserError(path.filename().string() + " nemá celé sektory CD.");
        out.push_back({path, 0, size / SECTOR});
    }
    if (out.empty()) throw UserError("V CUE chybí odkaz na soubor BIN.");
    return out;
}

static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

static std::vector<Piece> nrg_pieces(const fs::path& path) {
    auto data = read_file(path);
    size_t n = data.size();
    uint64_t chunks;
    if (n >= 12 && !std::memcmp(&data[n - 12], "NER5", 4)) chunks = be64(&data[n - 8]);
    else if (n >= 8 && !std::memcmp(&data[n - 8], "NERO", 4)) chunks = be32(&data[n - 4]);
    else throw UserError("NRG nemá platnou hlavičku Nero.");
    for (size_t at = chunks; at + 8 <= n;) {
        std::string kind(reinterpret_cast<char*>(&data[at]), 4);
        uint32_t size = be32(&data[at + 4]);
        if (size > n - at - 8) throw UserError("NRG je poškozený.");
        const uint8_t* body = &data[at + 8];
        if (kind == "DAOX" || kind == "DAOI") {
            if (size < 22) throw UserError("NRG je poškozený.");
            int first = body[20], last = body[21];
            size_t entry = kind == "DAOX" ? 42 : 30;
            if (last < first || 22 + size_t(last - first + 1) * entry > size) throw UserError("NRG je poškozený.");
            std::vector<std::array<uint64_t, 3>> tracks;
            for (int i = 0; i <= last - first; ++i) {
                const uint8_t* row = body + 22 + i * entry;
                if ((row[12] << 8 | row[13]) != SECTOR) throw UserError("NRG nemá surové sektory 2352 B. Ulož obraz jako BIN/CUE.");
                if (kind == "DAOX") tracks.push_back({be64(row + 18), be64(row + 26), be64(row + 34)});
                else tracks.push_back({be32(row + 18), be32(row + 22), be32(row + 26)});
            }
            uint64_t start = tracks.front()[1], end = tracks.back()[2];
            if (start > end || end > n) throw UserError("NRG je poškozený.");
            return {{path, start, (end - start) / SECTOR}};
        }
        if (kind == "END!") break;
        at += 8 + size;
    }
    throw UserError("V NRG chybí rozložení stop (DAO). Ulož obraz jako BIN/CUE.");
}

static Stream open_disc(const fs::path& source, std::string& kind) {
    kind = detect_format(source);
    if (kind == "iso") throw UserError("ISO obsahuje jen data, chybí hudba ze stop 2 a 3. Vyber úplný obraz CD (BIN/CUE, IMG/CCD, MDF/MDS, NRG).");
    if (kind == "chd") throw UserError("CHD zatím neumíme. Převeď ho na BIN/CUE příkazem: chdman extractcd -i hra.chd -o hra.cue");
    Stream s;
    if (kind == "cue") {
        auto cue = lower_ext(source) == "cue" ? source : sibling(source, {"cue"});
        if (cue.empty()) throw UserError("Vedle BIN chybí CUE se stejným jménem.");
        s.pieces = cue_pieces(cue);
    } else if (kind == "ccd") {
        auto img = lower_ext(source) == "img" ? source : sibling(source, {"img"});
        if (img.empty()) throw UserError("Vedle CCD chybí soubor IMG.");
        auto size = fs::file_size(img);
        if (size % SECTOR) throw UserError("IMG nemá celé sektory CD.");
        s.pieces = {{img, 0, size / SECTOR}};
    } else if (kind == "mds") {
        auto mdf = lower_ext(source) == "mdf" ? source : sibling(source, {"mdf"});
        if (mdf.empty()) throw UserError("Vedle MDS chybí soubor MDF.");
        auto size = fs::file_size(mdf);
        s.stride = size % SECTOR == 0 ? SECTOR : size % SUB_SECTOR == 0 ? SUB_SECTOR : 0;
        if (!s.stride) throw UserError("MDF nemá celé sektory CD.");
        s.pieces = {{mdf, 0, size / s.stride}};
    } else {
        s.pieces = nrg_pieces(source);
    }
    return s;
}

static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

// Finds a file in the ISO9660 filesystem of the data track (searches two directory levels).
static bool iso_find(const Stream& s, const std::string& name, uint32_t& lba, uint32_t& size) {
    auto pvd = s.user_data(16);
    if (std::memcmp(&pvd[1], "CD001", 5)) return false;
    std::vector<std::pair<uint32_t, uint32_t>> dirs{{le32(&pvd[156 + 2]), le32(&pvd[156 + 10])}};
    for (int depth = 0; depth < 2 && !dirs.empty(); ++depth) {
        std::vector<std::pair<uint32_t, uint32_t>> next;
        for (auto [dir, length] : dirs) {
            for (uint32_t sector = 0; sector < (length + 2047) / 2048 && sector < 64; ++sector) {
                auto block = s.user_data(dir + sector);
                for (size_t at = 0; at < 2048 && block[at];) {
                    uint8_t len = block[at];
                    if (len < 34 || at + len > 2048 || 33u + block[at + 32] > len) break;  // malformed record
                    uint8_t nlen = block[at + 32];
                    std::string entry(reinterpret_cast<char*>(&block[at + 33]), nlen);
                    if (auto semi = entry.find(';'); semi != std::string::npos) entry.resize(semi);
                    for (auto& ch : entry) ch = char(toupper(ch));
                    uint32_t elba = le32(&block[at + 2]), esize = le32(&block[at + 10]);
                    bool directory = block[at + 25] & 2;
                    if (!directory && entry == name) { lba = elba; size = esize; return true; }
                    if (directory && nlen > 1) next.push_back({elba, esize});
                    at += len;
                }
            }
        }
        dirs = next;
    }
    return false;
}

// Recognise the edition by one game file, never by a whole-image checksum or sizes: rips from different
// drives or tools differ in offsets, gaps and padding while the game files are identical.
struct Layout { uint64_t data_end = 0, gap = 0, audio = 0; };

static bool has_sync(const Stream& s, uint64_t index) {
    static const uint8_t sync[12] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    return !std::memcmp(s.raw(index).data(), sync, 12);
}

static Layout check_disc(const Stream& s, const DiscPins& pins, const Emit& emit) {
    auto pvd = s.user_data(16);
    if (std::memcmp(&pvd[1], "CD001", 5))
        throw UserError("V obrazu chybí datová stopa s hrou. Je to opravdu CD Horkého léta?");
    uint32_t lba, size;
    if (!iso_find(s, pins.key_file, lba, size)) throw UserError("Na CD chybí soubor " + pins.key_file + ".");
    hl::Sha256 hash;
    for (uint32_t done = 0, k = 0; done < size; ++k) {
        auto block = s.user_data(lba + k);
        uint32_t n = std::min<uint32_t>(2048, size - done);
        hash.update(block.data(), n);
        done += n;
    }
    if (hash.hex() != pins.key_sha256) throw UserError("Soubor " + pins.key_file + " nesedí. Tohle vydání Horkého léta zatím nepodporujeme.");
    emit("[detail] " + pins.key_file + " sedí, datová stopa odpovídá podporovanému vydání");
    // The data track ends where sectors stop carrying the Mode 1 sync pattern; audio follows.
    Layout out;
    uint64_t at = std::min<uint64_t>(le32(&pvd[80]), s.sectors());
    while (at < s.sectors() && has_sync(s, at)) ++at;
    out.data_end = at;
    // A stored 2 s gap before track 2 is 150 sectors of digital silence.
    bool silent = s.sectors() >= at + PREGAP;
    for (uint64_t k = 0; silent && k < PREGAP; ++k) {
        auto sector = s.raw(at + k);
        silent = std::all_of(sector.begin(), sector.end(), [](uint8_t b) { return b == 0; });
    }
    out.gap = silent ? PREGAP : 0;
    out.audio = s.sectors() - at - out.gap;
    if (out.audio < 75 * 60) throw UserError("V obrazu chybí hudební stopy. Vyber úplný obraz CD se všemi stopami.");
    emit("[detail] Data " + std::to_string(out.data_end) + " sektorů, hudba " + std::to_string(out.audio) + " sektorů" +
         (out.gap ? ", uložená mezera před stopou 2" : ""));
    return out;
}

std::string identify_disc(const fs::path& source, const DiscPins& pins, const Emit& emit) {
    std::string kind;
    auto s = open_disc(source, kind);
    emit("[detail] Rozpoznaný formát: " + format_name(kind) + ", " + std::to_string(s.sectors()) + " sektorů");
    check_disc(s, pins, emit);
    return kind;
}

static std::string msf_text(uint64_t sectors) {
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d:%02d", int(sectors / 4500), int(sectors / 75 % 60), int(sectors % 75));
    return text;
}

static fs::path write_cue(const fs::path& work, const DiscPins& pins, uint64_t data_end) {
    auto value = [&](size_t row) { return uint64_t(msf(pins.track_layout.at(row).substr(pins.track_layout.at(row).rfind(' ') + 1))); };
    uint64_t track2 = value(4), index0 = value(6) - track2, index1 = value(7) - track2;
    std::string text = "FILE \"disc.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
                       "  TRACK 02 AUDIO\n    PREGAP 00:02:00\n    INDEX 01 " + msf_text(data_end) + "\n"
                       "  TRACK 03 AUDIO\n    INDEX 00 " + msf_text(data_end + index0) + "\n    INDEX 01 " + msf_text(data_end + index1) + "\n";
    write_file(work / "disc.cue", text.data(), text.size());
    return work / "disc.cue";
}

fs::path normalize_disc(const fs::path& source, const fs::path& work, const DiscPins& pins, const Emit& emit) {
    std::string kind;
    auto s = open_disc(source, kind);
    emit("[detail] Rozpoznaný formát: " + format_name(kind) + ", " + std::to_string(s.sectors()) + " sektorů");
    auto layout = check_disc(s, pins, emit);
    uint64_t gap = layout.gap;
    fs::create_directories(work);
    auto bin = work / "disc.bin";
    remove_tree(bin);
    bool single = s.pieces.size() == 1 && !gap && s.stride == SECTOR && s.pieces[0].offset == 0;
    std::error_code ec;
    if (single) fs::create_hard_link(s.pieces[0].path, bin, ec);
    if (!single || ec) {
        if (single) fs::copy_file(s.pieces[0].path, bin);
        else {
            std::ofstream out(bin, std::ios::binary);
            s.read(layout.data_end, gap, [&](const uint8_t* sector) { out.write(reinterpret_cast<const char*>(sector), SECTOR); });
        }
    }
    return write_cue(work, pins, layout.data_end);
}

#ifdef _WIN32
std::vector<Drive> optical_drives() {
    std::vector<Drive> out;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask >> i & 1)) continue;
        wchar_t root[] = {wchar_t(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_CDROM) continue;
        wchar_t label[261]{};
        GetVolumeInformationW(root, label, 261, nullptr, nullptr, nullptr, nullptr, 0);
        char narrow[600]{};
        WideCharToMultiByte(CP_UTF8, 0, label, -1, narrow, sizeof narrow, nullptr, nullptr);
        out.push_back({std::string(1, char('A' + i)) + ":", narrow});
    }
    return out;
}

fs::path rip_drive(const std::string& letter, const fs::path& work, const DiscPins& pins, const Emit& emit) {
    std::string device = "\\\\.\\" + letter;
    HANDLE h = CreateFileA(device.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw UserError("Mechanika " + letter + " nejde otevřít. Je v ní CD?");
    struct Closer { HANDLE h; ~Closer() { CloseHandle(h); } } closer{h};
    CDROM_TOC toc{};
    DWORD got = 0;
    if (!DeviceIoControl(h, IOCTL_CDROM_READ_TOC, nullptr, 0, &toc, sizeof toc, &got, nullptr))
        throw UserError("Z CD nejde přečíst obsah (TOC).");
    int count = toc.LastTrack - toc.FirstTrack + 1;
    auto lba = [&](int i) { auto& a = toc.TrackData[i].Address; return (a[1] * 60 + a[2]) * 75 + a[3] - 150; };
    auto data = [&](int i) { return (toc.TrackData[i].Control & 4) != 0; };
    if (count != 3 || !data(0) || data(1) || data(2)) throw UserError("Tohle CD nemá rozložení Horkého léta (1 datová a 2 zvukové stopy).");
    int track2 = lba(1), leadout = lba(3), data_end = track2 - int(PREGAP);
    fs::create_directories(work);
    std::ofstream out(work / "disc.bin", std::ios::binary);
    std::vector<uint8_t> cooked(16 * 2048), raw(16 * SECTOR);
    uint64_t total = uint64_t(data_end) + (leadout - track2), done = 0;
    for (int at = 0; at < data_end; at += 16) {
        int n = std::min(16, data_end - at);
        LARGE_INTEGER offset; offset.QuadPart = int64_t(at) * 2048;
        SetFilePointerEx(h, offset, nullptr, FILE_BEGIN);
        if (!ReadFile(h, cooked.data(), DWORD(n * 2048), &got, nullptr) || got != DWORD(n * 2048))
            throw UserError("Datovou stopu nejde přečíst (sektor " + std::to_string(at) + "). Zkus CD očistit.");
        for (int k = 0; k < n; ++k) {
            uint8_t sector[SECTOR]{};
            std::memset(sector + 1, 0xff, 10);
            int address = at + k + 150;
            auto bcd = [](int v) { return uint8_t((v / 10) << 4 | v % 10); };
            sector[12] = bcd(address / 4500); sector[13] = bcd(address / 75 % 60); sector[14] = bcd(address % 75); sector[15] = 1;
            std::memcpy(sector + 16, &cooked[size_t(k) * 2048], 2048);
            out.write(reinterpret_cast<char*>(sector), SECTOR);
        }
        done += n;
        emit("Čtu CD data: " + std::to_string(done) + "/" + std::to_string(total) + " sektorů");
        check_cancelled();
    }
    for (int at = track2; at < leadout; at += 16) {
        int n = std::min(16, leadout - at);
        RAW_READ_INFO info{};
        info.DiskOffset.QuadPart = int64_t(at) * 2048;
        info.SectorCount = n;
        info.TrackMode = CDDA;
        if (!DeviceIoControl(h, IOCTL_CDROM_RAW_READ, &info, sizeof info, raw.data(), DWORD(n * SECTOR), &got, nullptr))
            throw UserError("Zvukovou stopu nejde přečíst (sektor " + std::to_string(at) + "). Zkus CD očistit.");
        out.write(reinterpret_cast<char*>(raw.data()), std::streamsize(n * SECTOR));
        done += n;
        emit("Čtu CD data: " + std::to_string(done) + "/" + std::to_string(total) + " sektorů");
        check_cancelled();
    }
    out.close();
    return write_cue(work, pins, uint64_t(data_end));
}
#else
std::vector<Drive> optical_drives() { return {}; }
fs::path rip_drive(const std::string&, const fs::path&, const DiscPins&, const Emit&) {
    throw UserError("Čtení CD mechaniky je dostupné jen ve Windows.");
}
#endif
}
