// The finished game's icon, cut from the player's own main character frame at build time.
// No game pixels live in the repository; the tile behind the character is drawn here.
#include "image.h"
#include "pipeline.h"
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

namespace hb {
#ifdef _WIN32
constexpr bool WINDOWS_GAME = true;
#else
constexpr bool WINDOWS_GAME = false;
#endif
static Image downscale(const Image& in, int size) {  // box filter
    Image out(size, size, 4);
    double s = double(in.w) / size;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double acc[4]{};
            int n = 0;
            for (int yy = int(y * s); yy < int((y + 1) * s); ++yy)
                for (int xx = int(x * s); xx < int((x + 1) * s); ++xx, ++n) {
                    const uint8_t* p = in.at(xx, yy);
                    double a = p[3] / 255.0;
                    for (int k = 0; k < 3; ++k) acc[k] += p[k] * a;
                    acc[3] += a;
                }
            uint8_t* d = out.at(x, y);
            if (acc[3] > 0) for (int k = 0; k < 3; ++k) d[k] = uint8_t(std::lround(acc[k] / acc[3]));
            d[3] = uint8_t(std::lround(acc[3] / n * 255));
        }
    return out;
}

static Image tile(int size) {
    Image out(size, size, 4);
    double inset = 40, radius = size / 5.0, border = size / 64.0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double dx = std::max({inset + radius - x, x - (size - inset - radius), 0.0});
            double dy = std::max({inset + radius - y, y - (size - inset - radius), 0.0});
            double edge = radius - std::hypot(dx, dy);
            double box = std::min({x - inset, size - inset - x, y - inset, size - inset - y});
            double inside = (dx > 0 && dy > 0) ? edge : box;
            if (inside < 0) continue;
            uint8_t* p = out.at(x, y);
            double t = double(y) / size;
            if (inside < border) { p[0] = p[1] = p[2] = 0; }
            else if (t < 0.62) {
                double band = std::floor(t / 0.62 * 8) / 8;
                p[0] = uint8_t(255 - 60 * band); p[1] = uint8_t(176 + 40 * band); p[2] = uint8_t(64 + 150 * band);
                if (std::hypot(x - size * 0.72, y - size * 0.28) < size * 0.14) { p[0] = 255; p[1] = 232; p[2] = 96; }
            } else if (t < 0.78) {
                bool odd = (y / 12) % 2;
                p[0] = odd ? 24 : 32; p[1] = odd ? 112 : 128; p[2] = odd ? 176 : 192;
            } else { p[0] = 232; p[1] = 200; p[2] = 120; }
            p[3] = 255;
        }
    return out;
}

static Image icon_image(const fs::path& work) {
    auto frame = load_png(work / "frames" / "C1-VSTAV.FLC" / "frame-15.png", 4);
    auto head = crop(frame, 0, 0, frame.w, std::min(frame.h, 62));
    std::array<int, 4> box;
    if (bbox(head, box)) head = crop(head, box[0], box[1], box[2] - box[0], box[3] - box[1]);
    const int size = 1024;
    int scale = std::max(1, int(size * 0.62 / std::max(head.w, head.h)));
    head = resize_nearest(head, head.w * scale, head.h * scale);
    auto base = tile(size);
    Image layer(size, size, 4);
    paste(layer, head, (size - head.w) / 2, size - 40 - size / 14 - head.h);
    alpha_composite(base, layer);
    return base;
}

static void be32(Bytes& b, uint32_t v) { for (int i = 3; i >= 0; --i) b.push_back(uint8_t(v >> (8 * i))); }
static void le16(Bytes& b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
static void le32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }

static Bytes icns(const Image& image) {
    Bytes body;
    for (auto [type, size] : std::initializer_list<std::pair<const char*, int>>{
             {"icp4", 16}, {"icp5", 32}, {"icp6", 64}, {"ic07", 128}, {"ic08", 256}, {"ic09", 512}, {"ic10", 1024}}) {
        auto png = encode_png(size == 1024 ? image : downscale(image, size));
        body.insert(body.end(), type, type + 4);
        be32(body, uint32_t(png.size() + 8));
        body.insert(body.end(), png.begin(), png.end());
    }
    Bytes out{'i', 'c', 'n', 's'};
    be32(out, uint32_t(body.size() + 8));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static std::vector<Bytes> ico_images(const Image& image) {
    std::vector<Bytes> out;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) out.push_back(encode_png(downscale(image, size)));
    return out;
}

static Bytes ico(const std::vector<Bytes>& images) {
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    Bytes out;
    le16(out, 0); le16(out, 1); le16(out, uint16_t(images.size()));
    uint32_t offset = uint32_t(6 + 16 * images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        out.push_back(uint8_t(sizes[i] & 255)); out.push_back(uint8_t(sizes[i] & 255)); out.push_back(0); out.push_back(0);
        le16(out, 1); le16(out, 32); le32(out, uint32_t(images[i].size())); le32(out, offset);
        offset += uint32_t(images[i].size());
    }
    for (auto& img : images) out.insert(out.end(), img.begin(), img.end());
    return out;
}

#ifndef _WIN32
static void plist_icon(const fs::path& app) {
    // Info.plist is XML from our own template: add CFBundleIconFile once.
    auto plist_path = app / "Contents" / "Info.plist";
    auto bytes = read_file(plist_path);
    std::string plist(bytes.begin(), bytes.end());
    if (plist.find("CFBundleIconFile") == std::string::npos) {
        auto at = plist.rfind("</dict>");
        if (at == std::string::npos) throw std::runtime_error("Info.plist");
        plist.insert(at, "\t<key>CFBundleIconFile</key>\n\t<string>AppIcon</string>\n");
        write_file(plist_path, plist.data(), plist.size());
    }
}
#endif

static fs::path resources_of(const fs::path& app) { return WINDOWS_GAME ? app / "Resources" : app / "Contents" / "Resources"; }

static void apply_game_icon(const Image& image, const fs::path& app);

fs::path make_game_icon(const fs::path& work, const fs::path& app) {
    auto image = icon_image(work);
    auto preview = work / "game-icon.png";
    save_png(image, preview);
    save_png(image, resources_of(app) / "game-icon.png");  // kept so an engine update can re-apply it
    apply_game_icon(image, app);
    return preview;
}

void carry_game_icon(const fs::path& old_app, const fs::path& new_app) {
    auto png = resources_of(new_app) / "game-icon.png";
    if (fs::exists(png)) { apply_game_icon(load_png(png, 4), new_app); return; }
#ifdef _WIN32
    // Games from builder 1.0.0 keep their icon only inside the exe: copy those resources across.
    HMODULE from = LoadLibraryExW((old_app / "HorkeLeto.exe").wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!from) return;
    HANDLE update = BeginUpdateResourceW((new_app / "HorkeLeto.exe").wstring().c_str(), FALSE);
    if (!update) { FreeLibrary(from); return; }
    for (WORD id = 1; id <= 16; ++id) {
        HRSRC r = FindResourceW(from, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(3));
        if (!r) continue;
        HGLOBAL data = LoadResource(from, r);
        UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(id), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), LockResource(data), SizeofResource(from, r));
    }
    if (HRSRC g = FindResourceW(from, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14))) {
        HGLOBAL data = LoadResource(from, g);
        UpdateResourceW(update, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), LockResource(data), SizeofResource(from, g));
    }
    FreeLibrary(from);
    EndUpdateResourceW(update, FALSE);
#else
    (void)old_app;
    if (fs::exists(resources_of(new_app) / "AppIcon.icns")) plist_icon(new_app);
#endif
}

static void apply_game_icon(const Image& image, const fs::path& app) {
#ifdef _WIN32
    auto images = ico_images(image);
    HANDLE update = BeginUpdateResourceW((app / "HorkeLeto.exe").wstring().c_str(), FALSE);
    if (!update) throw std::runtime_error("BeginUpdateResource");
    Bytes group;
    le16(group, 0); le16(group, 1); le16(group, uint16_t(images.size()));
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    for (size_t i = 0; i < images.size(); ++i) {
        UpdateResourceW(update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(WORD(i + 1)), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), images[i].data(), DWORD(images[i].size()));
        group.push_back(uint8_t(sizes[i] & 255)); group.push_back(uint8_t(sizes[i] & 255)); group.push_back(0); group.push_back(0);
        le16(group, 1); le16(group, 32); le32(group, uint32_t(images[i].size())); le16(group, uint16_t(i + 1));
    }
    UpdateResourceW(update, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), group.data(), DWORD(group.size()));
    if (!EndUpdateResourceW(update, FALSE)) throw std::runtime_error("EndUpdateResource");
#else
    auto file = icns(image);
    write_file(app / "Contents" / "Resources" / "AppIcon.icns", file.data(), file.size());
    plist_icon(app);
#endif
}
}
