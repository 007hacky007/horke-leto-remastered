// Developer check: decrypt one real art entry and identify a real disc image.
#include "crypto.h"
#include "disc.h"
#include "zip.h"
#include <cstdio>
#include <regex>

using namespace hb;

static Bytes unhex(const std::string& h) { Bytes b(h.size() / 2); for (size_t i = 0; i < b.size(); ++i) b[i] = uint8_t(std::stoi(h.substr(2 * i, 2), nullptr, 16)); return b; }

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "identify") {
        try { auto kind = identify_disc(argv[3], disc_pins(read_json(argv[2])), [](const std::string& m) { std::puts(m.c_str()); }); std::printf("OK %s\n", kind.c_str()); }
        catch (const std::exception& e) { std::printf("REJECTED: %s\n", e.what()); }
        return 0;
    }
    if (argc < 5) { std::puts("usage: hb-selftest pins.json art.zip GFX.GRP disc.cue"); return 2; }
    auto pins = read_json(argv[1]);
    auto archive = read_file(argv[2]);
    json manifest;
    std::map<std::string, ZipEntry> entries;
    for (auto& e : zip_entries(archive)) entries[e.name] = e;
    manifest = json::parse(zip_read(archive, entries.at("manifest.json")));
    auto grp = read_file(argv[3]);
    // Find the first encrypted entry and its original GRP member.
    auto it = manifest.begin();
    std::string name = it.key();
    auto row = it.value();
    uint32_t used = grp[28] | grp[29] << 8 | grp[30] << 16 | uint32_t(grp[31]) << 24;
    Bytes entry;
    for (uint32_t i = 0; i < used; ++i) {
        size_t at = 44 + i * 30;
        std::string member(reinterpret_cast<char*>(&grp[at]), strnlen(reinterpret_cast<char*>(&grp[at]), 14));
        for (auto& c : member) c = char(toupper(c));
        if (member != name) continue;
        uint32_t off = grp[at + 14] | grp[at + 15] << 8 | grp[at + 16] << 16 | uint32_t(grp[at + 17]) << 24;
        uint32_t size = grp[at + 18] | grp[at + 19] << 8 | grp[at + 20] << 16 | uint32_t(grp[at + 21]) << 24;
        entry.assign(grp.begin() + off, grp.begin() + off + size);
    }
    std::string ctx = std::string("FLC redraw") + '\0' + name;
    Bytes aad(ctx.begin(), ctx.end());
    std::string salt_text = "Horke Leto Remastered art v1";
    auto key = hkdf32(entry, Bytes(salt_text.begin(), salt_text.end()), aad);
    auto sealed = zip_read(archive, entries.at("encrypted/" + name + ".enc"));
    auto nonce = unhex(row.at("nonce"));
    Bytes plain;
    bool ok = xchacha20poly1305_decrypt(plain, sealed, aad, nonce.data(), key.data());
    std::printf("decrypt %s: %s, plaintext sha matches: %s\n", name.c_str(), ok ? "ok" : "FAILED",
                ok && sha_bytes(plain) == row.at("plaintext_sha256").get<std::string>() ? "yes" : "no");
    sealed[0] ^= 1;
    std::printf("tampered rejected: %s\n", !xchacha20poly1305_decrypt(plain, sealed, aad, nonce.data(), key.data()) ? "yes" : "no");
    auto kind = identify_disc(argv[4], disc_pins(pins), [](const std::string& m) { std::puts(m.c_str()); });
    std::printf("disc: %s\n", kind.c_str());
    return 0;
}
