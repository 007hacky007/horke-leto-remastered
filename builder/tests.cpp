// Known-answer tests for the builder's own crypto and image helpers (run by ctest).
#include "crypto.h"
#include "disc.h"
#include "image.h"
#include <cstdio>

using namespace hb;

static Bytes unhex(const std::string& h) {
    Bytes b(h.size() / 2);
    for (size_t i = 0; i < b.size(); ++i) b[i] = uint8_t(std::stoi(h.substr(2 * i, 2), nullptr, 16));
    return b;
}
static std::string hex(const Bytes& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto v : b) { s += d[v >> 4]; s += d[v & 15]; }
    return s;
}
static int failures = 0;
static void expect(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

int main() {
    // RFC 5869 test case 1, first 32 bytes of OKM.
    expect(hex(hkdf32(Bytes(22, 0x0b), unhex("000102030405060708090a0b0c"), unhex("f0f1f2f3f4f5f6f7f8f9"))) ==
               "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf", "hkdf rfc5869 case 1");

    // Produced with libsodium crypto_aead_xchacha20poly1305_ietf_encrypt.
    uint8_t key[32], nonce[24];
    for (int i = 0; i < 32; ++i) key[i] = uint8_t(i);
    for (int i = 0; i < 24; ++i) nonce[i] = uint8_t(100 + i);
    std::string aad_text = "FLC redraw";
    Bytes aad(aad_text.begin(), aad_text.end());
    auto sealed = unhex("341c8bdb9a0ee5e7f35fbd4bc31cc9e92dde31058b5ee690bf6244c5522a1d638b6a2c104b9735c33414080bf16980bf91a120a2bb99");
    Bytes plain;
    expect(xchacha20poly1305_decrypt(plain, sealed, aad, nonce, key) &&
               hex(plain) == "486f726b65206c65746f207465737420766563746f722c203337206279746573206c6f6e6721",
           "xchacha20poly1305 libsodium vector");
    sealed[5] ^= 1;
    expect(!xchacha20poly1305_decrypt(plain, sealed, aad, nonce, key), "xchacha20poly1305 rejects tampering");

    expect(hex(hmac_sha256(Bytes{'k', 'e', 'y'}, Bytes{'a'})).size() == 64, "hmac length");

    Image img(4, 4);
    img.px[(1 * 4 + 2) * 4 + 3] = 255;
    std::array<int, 4> box{};
    expect(bbox(img, box) && box[0] == 2 && box[1] == 1 && box[2] == 3 && box[3] == 2, "alpha bbox");

    auto dir = fs::temp_directory_path() / "hb-tests";
    fs::create_directories(dir);
    { Bytes zeros(4096, 0); write_file(dir / "a.iso", zeros.data(), zeros.size()); }
    std::string chd_head = "MComprHD________";
    write_file(dir / "b.img", chd_head.data(), chd_head.size());
    expect(detect_format(dir / "a.iso") == "iso" && detect_format(dir / "b.img") == "chd", "format detection");
    bool rejected = false;
    try { identify_disc(dir / "a.iso", DiscPins{}, [](const std::string&) {}); } catch (const UserError&) { rejected = true; }
    expect(rejected, "iso images are rejected");
    remove_tree(dir);

    std::printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
