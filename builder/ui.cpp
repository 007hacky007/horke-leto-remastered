// The builder window: Czech, Windows 98 look, drawn with SDL3. Four screens: source, checks, build, done.
#include "disc.h"
#include "image.h"
#include "pipeline.h"
#include "preflight.h"
#include "session.h"
#include "taskbar.h"
#include "update.h"
#include "player.h"
#include <string>
extern std::string replaced_version;  // main.cpp
#include <ctime>
#include <optional>
#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#include "stb_truetype.h"

namespace hb {
struct Embedded { const char* name; const unsigned char* data; size_t size; };
const Embedded* find_embedded(const char* name);
}

namespace {
using namespace hb;

constexpr int W = 960, H = 640;
const SDL_Color FACE{192, 192, 192, 255}, LIGHT{255, 255, 255, 255}, SHADOW{128, 128, 128, 255}, DARK{0, 0, 0, 255},
    NAVY{0, 0, 128, 255}, BLUE2{16, 132, 208, 255}, DOS{0, 0, 168, 255}, GREY_TEXT{64, 64, 64, 255}, DISABLED{128, 128, 128, 255};

SDL_Renderer* R = nullptr;
float density = 1;

void color(SDL_Color c) { SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a); }
void fill(float x, float y, float w, float h, SDL_Color c) { color(c); SDL_FRect r{x, y, w, h}; SDL_RenderFillRect(R, &r); }
void line(float x1, float y1, float x2, float y2, SDL_Color c) { color(c); SDL_RenderLine(R, x1, y1, x2, y2); }

// Windows 95/98 bevels: two pixels, light from the top left.
const SDL_Color HILITE{223, 223, 223, 255};
void edge(float x, float y, float w, float h, SDL_Color tl, SDL_Color br) {
    line(x, y, x + w - 2, y, tl); line(x, y, x, y + h - 2, tl);
    line(x, y + h - 1, x + w - 1, y + h - 1, br); line(x + w - 1, y, x + w - 1, y + h - 1, br);
}
void bevel(float x, float y, float w, float h, bool sunken = false) {
    if (sunken) { edge(x, y, w, h, SHADOW, LIGHT); edge(x + 1, y + 1, w - 2, h - 2, DARK, HILITE); }
    else { edge(x, y, w, h, LIGHT, DARK); edge(x + 1, y + 1, w - 2, h - 2, HILITE, SHADOW); }
}
// A pressed push button: dark outer frame, grey inner shadow, label moves down right.
void pressed(float x, float y, float w, float h) { edge(x, y, w, h, DARK, LIGHT); edge(x + 1, y + 1, w - 2, h - 2, SHADOW, HILITE); }
void dotted(float x, float y, float w, float h) {
    for (float i = x; i < x + w; i += 2) { fill(i, y, 1, 1, DARK); fill(i, y + h - 1, 1, 1, DARK); }
    for (float i = y; i < y + h; i += 2) { fill(x, i, 1, 1, DARK); fill(x + w - 1, i, 1, 1, DARK); }
}

// ---- fonts ----
struct Font {
    SDL_Texture* atlas = nullptr;
    std::map<uint32_t, stbtt_packedchar> glyphs;
    float size = 12, ascent = 10;
    void load(const char* file, float px, bool crisp) {
        auto* data = find_embedded(file);
        size = px;
        float scale = density;
        const int side = 1024;
        std::vector<uint8_t> bitmap(side * side);
        stbtt_pack_context pack;
        stbtt_PackBegin(&pack, bitmap.data(), side, side, 0, 1, nullptr);
        std::vector<std::pair<int, int>> ranges{{32, 95}, {160, 223}, {0x2026, 1}, {0x2192, 1}, {0x2212, 1}, {0x25A0, 1}, {0x25BA, 1}, {0x2713, 1}, {0x25BC, 1}};
        for (auto [first, count] : ranges) {
            std::vector<stbtt_packedchar> chars(count);
            stbtt_PackFontRange(&pack, data->data, 0, px * scale, first, count, chars.data());
            for (int i = 0; i < count; ++i) glyphs[uint32_t(first + i)] = chars[i];
        }
        stbtt_PackEnd(&pack);
        stbtt_fontinfo info;
        stbtt_InitFont(&info, data->data, 0);
        int a, d, g;
        stbtt_GetFontVMetrics(&info, &a, &d, &g);
        ascent = a * stbtt_ScaleForPixelHeight(&info, px);
        std::vector<uint8_t> rgba(side * side * 4, 255);
        for (int i = 0; i < side * side; ++i) rgba[i * 4 + 3] = crisp ? (bitmap[i] >= 110 ? 255 : 0) : bitmap[i];
        atlas = SDL_CreateTexture(R, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, side, side);
        SDL_UpdateTexture(atlas, nullptr, rgba.data(), side * 4);
        SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(atlas, SDL_SCALEMODE_NEAREST);
    }
};
Font regular, bold, title, big, mono, tiny, small, heading, huge;

std::vector<uint32_t> decode(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = uint8_t(s[i]);
        uint32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 31; n = 2; } else if ((c >> 4) == 14) { cp = c & 15; n = 3; } else { cp = c & 7; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = cp << 6 | (uint8_t(s[i + k]) & 63);
        out.push_back(cp);
        i += n;
    }
    return out;
}

float text_width(Font& f, const std::string& s) {
    float w = 0;
    for (auto cp : decode(s)) { auto it = f.glyphs.find(cp); if (it == f.glyphs.end()) it = f.glyphs.find('?'); w += it->second.xadvance / density; }
    return w;
}

void text(Font& f, float x, float y, const std::string& s, SDL_Color c = DARK) {
    SDL_SetTextureColorMod(f.atlas, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(f.atlas, c.a);
    float pen = x;
    for (auto cp : decode(s)) {
        auto it = f.glyphs.find(cp);
        if (it == f.glyphs.end()) it = f.glyphs.find('?');
        auto& g = it->second;
        SDL_FRect src{float(g.x0), float(g.y0), float(g.x1 - g.x0), float(g.y1 - g.y0)};
        SDL_FRect dst{std::round((pen + g.xoff / density) * density) / density, std::round((y + f.ascent + g.yoff / density) * density) / density,
                      src.w / density, src.h / density};
        SDL_RenderTexture(R, f.atlas, &src, &dst);
        pen += g.xadvance / density;
    }
}

std::vector<std::string> wrap(Font& f, const std::string& s, float width) {
    std::vector<std::string> lines;
    std::string current, word;
    auto flush_word = [&] {
        std::string candidate = current.empty() ? word : current + " " + word;
        if (!current.empty() && text_width(f, candidate) > width) { lines.push_back(current); current = word; }
        else current = candidate;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flush_word();
        else if (ch == '\n') { flush_word(); lines.push_back(current); current.clear(); }
        else word += ch;
    }
    flush_word();
    if (!current.empty()) lines.push_back(current);
    return lines;
}

float paragraph(Font& f, float x, float y, float width, const std::string& s, SDL_Color c = DARK, float spacing = 1.35f) {
    for (auto& l : wrap(f, s, width)) { text(f, x, y, l, c); y += f.size * spacing; }
    return y;
}

std::string elide(Font& f, const std::string& s, float width) {
    if (text_width(f, s) <= width) return s;
    auto cps = decode(s);
    for (size_t cut = 1; cut < cps.size(); ++cut) {
        std::string tail;
        for (size_t i = cut; i < cps.size(); ++i) {
            uint32_t cp = cps[i];
            if (cp < 0x80) tail += char(cp);
            else if (cp < 0x800) { tail += char(0xC0 | cp >> 6); tail += char(0x80 | (cp & 63)); }
            else { tail += char(0xE0 | cp >> 12); tail += char(0x80 | (cp >> 6 & 63)); tail += char(0x80 | (cp & 63)); }
        }
        if (text_width(f, "…" + tail) <= width) return "…" + tail;
    }
    return s;
}

// ---- textures ----
SDL_Texture* texture(const Image& in, bool smooth = false) {
    auto image = convert(in, 4);
    auto* t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, image.w, image.h);
    SDL_UpdateTexture(t, nullptr, image.px.data(), image.w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    return t;
}
std::map<std::string, SDL_Texture*> icons;
SDL_Texture* icon(const std::string& name) {
    auto it = icons.find(name);
    if (it != icons.end()) return it->second;
    auto* data = find_embedded((name + ".png").c_str());
    return icons[name] = texture(load_png_memory(Bytes(data->data, data->data + data->size), 4));
}
void draw(SDL_Texture* t, float x, float y, float w, float h, double angle = 0) {
    SDL_FRect dst{x, y, w, h};
    if (angle) SDL_RenderTextureRotated(R, t, nullptr, &dst, angle, nullptr, SDL_FLIP_NONE);
    else SDL_RenderTexture(R, t, nullptr, &dst);
}
// Fit a texture into a box keeping its aspect ratio; returns the drawn rect.
SDL_FRect fit(SDL_Texture* t, float x, float y, float w, float h) {
    float tw, th;
    SDL_GetTextureSize(t, &tw, &th);
    float s = std::min(w / tw, h / th);
    SDL_FRect r{x + (w - tw * s) / 2, y + (h - th * s) / 2, tw * s, th * s};
    SDL_RenderTexture(R, t, nullptr, &r);
    return r;
}

// ---- input and widgets ----
struct Input { float x = 0, y = 0; bool down = false, pressed = false, released = false; float wheel = 0; bool enter = false; } in;
bool inside(float x, float y, float w, float h) { return in.x >= x && in.y >= y && in.x < x + w && in.y < y + h; }

bool button(float x, float y, float w, float h, const std::string& label, bool enabled = true, bool primary = false) {
    bool hot = enabled && inside(x, y, w, h);
    bool pushed = hot && in.down;
    fill(x, y, w, h, FACE);
    float bx = x, by = y, bw = w, bh = h;
    if (primary) {  // the default button wears a black frame
        color(DARK); SDL_FRect r{x, y, w, h}; SDL_RenderRect(R, &r);
        bx += 1; by += 1; bw -= 2; bh -= 2;
    }
    if (pushed) pressed(bx, by, bw, bh); else bevel(bx, by, bw, bh);
    float off = pushed ? 1 : 0;
    float tx = x + (w - text_width(regular, label)) / 2 + off, ty = y + (h - regular.size * 1.2f) / 2 + off;
    if (enabled) text(regular, tx, ty, label, DARK);
    else { text(regular, tx + 1, ty + 1, label, LIGHT); text(regular, tx, ty, label, SHADOW); }  // etched
    if (primary && enabled) dotted(bx + 4, by + 4, bw - 8, bh - 8);
    return hot && in.released;
}

bool radio(float x, float y, const std::string& label, bool on, bool enabled = true) {
    // 12 px round well drawn from pixel rows, as Windows 95 does.
    static const char* rows[] = {"    SSSS    ", "  SSDDDDSS  ", " SDDWWWWDDH ", " SDWWWWWWHH ", "SDWWWWWWWWH ", "SDWWWWWWWWH ",
                                 "SDWWWWWWWWH ", "SDWWWWWWWWH ", " SWWWWWWWWH ", " HHWWWWWWHH ", "  HHHHHHHH  ", "    HHHH    "};
    for (int j = 0; j < 12; ++j)
        for (int i = 0; i < 12; ++i) {
            char c = rows[j][i];
            if (c == ' ') continue;
            fill(x + i, y + 3 + j, 1, 1, c == 'S' ? SHADOW : c == 'D' ? DARK : c == 'H' ? LIGHT : (enabled ? LIGHT : FACE));
        }
    if (on) fill(x + 4, y + 7, 4, 4, enabled ? DARK : SHADOW);
    text(regular, x + 18, y, label, enabled ? DARK : SHADOW);
    return enabled && inside(x, y, 20 + text_width(regular, label), 18) && in.released;
}

bool checkbox(float x, float y, const std::string& label, bool on) {
    fill(x, y + 2, 13, 13, LIGHT);
    bevel(x, y + 2, 13, 13, true);
    if (on) {  // the classic 7 x 7 black tick
        static const int tick[7][2] = {{0, 2}, {1, 3}, {2, 4}, {3, 3}, {4, 2}, {5, 1}, {6, 0}};
        for (auto& p : tick) fill(x + 3 + p[0], y + 2 + 3 + p[1], 1, 3, DARK);
    }
    text(regular, x + 20, y, label);
    return inside(x, y, 20 + text_width(regular, label), 18) && in.released;
}

void field(float x, float y, float w, const std::string& value, bool enabled = true) {
    fill(x, y, w, 24, enabled ? LIGHT : FACE);
    bevel(x, y, w, 24, true);
    text(regular, x + 6, y + 4, elide(regular, value, w - 12), enabled ? DARK : DISABLED);
}

void group(float x, float y, float w, float h, const std::string& label) {
    color(SHADOW); SDL_FRect a{x, y + 8, w, h - 8}; SDL_RenderRect(R, &a);
    color(LIGHT); SDL_FRect b{x + 1, y + 9, w, h - 8}; SDL_RenderRect(R, &b);
    fill(x + 8, y, text_width(regular, label) + 8, 16, FACE);
    text(regular, x + 12, y, label);
}

void title_bar(float x, float y, float w, float h, const std::string& label, SDL_Texture* glyph) {
    for (int i = 0; i < int(w); ++i) {
        float t = i / w;
        line(x + i, y, x + i, y + h - 1, SDL_Color{Uint8(NAVY.r + (BLUE2.r - NAVY.r) * t), Uint8(NAVY.g + (BLUE2.g - NAVY.g) * t), Uint8(NAVY.b + (BLUE2.b - NAVY.b) * t), 255});
    }
    float tx = x + 6;
    if (glyph) { draw(glyph, x + 4, y + (h - 16) / 2, 16, 16); tx += 20; }
    text(bold, tx, y + (h - bold.size * 1.2f) / 2, label, LIGHT);
}

void segments(float x, float y, float w, float h, double fraction, int marquee = -1) {
    fill(x, y, w, h, LIGHT);
    bevel(x, y, w, h, true);
    float block = 9, gap = 2, ix = x + 3, iy = y + 3, ih = h - 6;
    int count = std::max(1, int((w - 6 + gap) / (block + gap)));
    int lit = int(count * std::clamp(fraction, 0.0, 1.0));
    for (int i = 0; i < count; ++i) {
        bool on = marquee >= 0 ? ((i - marquee % count + count) % count) < 5 : i < lit;
        if (on) fill(ix + i * (block + gap), iy, block, ih, NAVY);
    }
}

// ---- application state ----
struct Stage { const char* key; double share; const char* title; const char* why; const char* dialog; const char* glyph; };
const Stage STAGES[] = {
    {"read", 6, "Čteme herní data", "Herní data + audio (CD stopa 2 a 3)", "Načítání CD...", "disc-16"},
    {"extract", 6, "Vybalujeme obrázky", "Z archivu GFX.GRP dekódujeme všech 595 obrázků a animací FLC.", "Kopírování...", "folder-16"},
    {"download", 5, "Stahujeme remaster data", "Kontrolujeme kontrolní součet remasterovaných dat.", "Stahování...", "floppy-16"},
    {"decrypt", 5, "Dešifrování remaster dat", "Klíčem jsou obrázky z tvých herních dat (z tvé zálohy). Bez něj není možné remaster dešifrovat.", "Dešifrování remasterovaných obrázků...", "padlock-16"},
    {"upscale", 61, "Upscaling 3x", "Každý snímek z 640 x 480 na 1920 x 1440. Tato část zabere nejvíc času.", "Upscaling 3x", "magnifier-16"},
    {"remaster", 13, "Skládáme balíčky", "Spojujeme remaster s upscalovanými animacemi do Original a Remastered.", "Sestavování balíčků...", "gear-16"},
    {"app", 4, "Vytváříme hru", "Spouštěč, data, hudba, ikona z tvých herních dat a závěrečná kontrola.", "Vytváření aplikace...", "gear-16"},
};
int stage_index(const std::string& key) {
    for (int i = 0; i < 7; ++i) if (key == STAGES[i].key) return i;
    return -1;
}

struct LogLine { std::string text; bool detail; };

struct App {
    SDL_Window* window = nullptr;
    int page = 0;
    std::string image, output, archive;
    std::vector<Drive> drives;
    int drive = 0;
    bool use_drive = false, cleanup = true;
    std::string detected;
    std::mutex lock;
    std::deque<std::pair<int, std::string>> pending_paths;  // 0 image, 1 output, 2 archive
    std::vector<Check> checks;
    bool checking = false;
    std::deque<std::string> messages;
    std::thread worker;
    bool busy = false, failed = false, details = false, everything = false;
    int stage = -1;
    double fraction = 0;
    bool marquee = true;
    std::string caption, count, speed, eta, error;
    std::chrono::steady_clock::time_point started, stage_started;
    SDL_Texture *picture = nullptr, *result = nullptr, *character = nullptr;
    Uint64 picture_at = 0;
    SDL_FRect focus{};  // the detail inside the picture texture; the texture itself is only a small surrounding cut-out
    std::string shown;
    std::vector<LogLine> log;
    std::vector<std::string> recent;  // resources finished in the current stage
    struct Thumb { SDL_Texture* texture; std::vector<SDL_Color> cells; };  // cells: 12 x 9 colours
    std::deque<Thumb> thumbs;  // recently decrypted redraws
    int trip_start = 0;        // items done when the carried batch started
    fs::path next_source, next_result;  // newest finished frame, picked up by the chopping scene when it is ready
    float log_scroll = 0;
    fs::path work, result_path;
    Uint64 last_watch = 0;
    std::optional<Session> resume;  // unfinished build offered on the first screen
    std::string resume_error;
    Session session;                // the running build, saved for the next start
    Uint64 session_saved = 0;
    bool attention = false;
    // Self-update: found -> downloading -> starting (quit) or failed.
    enum class Update { None, Found, Downloading, Starting, Failed } update = Update::None;
    UpdateInfo update_info;
    Uint64 update_at = 0;       // when the current update state began (animations)
    double update_fraction = 0;
    std::string update_error, replaced;
    fs::path update_file;
    bool update_dismissed = false, quit = false;
    std::thread update_worker;
    // Engine update of an already built game.
    enum class GameUpdate { None, Offer, Running, Done, Failed } game = GameUpdate::None;
    std::deque<GameInfo> outdated;  // found at start; the first one is offered
    GameInfo game_info;
    std::string game_status, game_error;
    Uint64 game_at = 0;
    std::thread game_worker;
    // CD player on the build screen.
    CdPlayer player;
    Uint64 volume_shown = 0, player_checked = 0;
    std::array<float, 5> peaks{};
    std::array<Uint64, 5> peak_at{};
    double cd_angle = 0;
} app;


// A redraw miniature cropped to its visible content, plus a 12 x 9 colour grid of it.
App::Thumb thumbnail(const fs::path& path) {
    auto image = load_png(path, 4);
    std::array<int, 4> box;
    if (bbox(image, box)) image = crop(image, box[0], box[1], box[2] - box[0], box[3] - box[1]);
    App::Thumb out{texture(image, true), {}};
    for (int gy = 0; gy < 9; ++gy)
        for (int gx = 0; gx < 12; ++gx) {
            const uint8_t* p = image.at(std::min(image.w - 1, (gx * 2 + 1) * image.w / 24), std::min(image.h - 1, (gy * 2 + 1) * image.h / 18));
            out.cells.push_back(p[3] > 64 ? SDL_Color{p[0], p[1], p[2], 255} : SDL_Color{0, 0, 0, 0});
        }
    return out;
}

// A small, busy detail of a frame, so the animations never show a whole scene or character to a new player.
// Returns {x, y, w, h} of the detail; `context` gets a cut-out twice its size around it for the zoom-in.
std::array<int, 4> detail_region(const Image& image, std::array<int, 4>& context) {
    int cw = std::min(image.w, 112), ch = std::min(image.h, 56);
    std::array<int, 4> best{(image.w - cw) / 2, (image.h - ch) / 2, cw, ch};
    if (image.w > cw || image.h > ch) {
        // Score candidate windows by edge energy over opaque pixels; pick randomly among the busiest ones.
        auto lum = [&](int x, int y) { const uint8_t* p = image.at(x, y); return p[3] < 128 ? -1 : (p[0] * 3 + p[1] * 6 + p[2]) / 10; };
        std::vector<std::pair<double, std::array<int, 4>>> scored;
        for (int y = 0; y + ch <= image.h; y += std::max(1, ch / 2))
            for (int x = 0; x + cw <= image.w; x += std::max(1, cw / 2)) {
                double energy = 0; int opaque = 0, samples = 0;
                for (int yy = y; yy < y + ch - 2; yy += 2)
                    for (int xx = x; xx < x + cw - 2; xx += 2) {
                        int a = lum(xx, yy), b = lum(xx + 2, yy), c = lum(xx, yy + 2);
                        ++samples;
                        if (a < 0) continue;
                        ++opaque;
                        if (b >= 0) energy += std::abs(a - b);
                        if (c >= 0) energy += std::abs(a - c);
                    }
                if (samples && opaque * 10 >= samples * 9) scored.push_back({energy, {x, y, cw, ch}});
            }
        if (!scored.empty()) {
            std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
            static std::mt19937 pick(1998);
            best = scored[pick() % std::max<size_t>(1, scored.size() / 3)].second;
        }
    }
    int xw = std::min(image.w, cw * 2), xh = std::min(image.h, ch * 2);
    int xx = std::clamp(best[0] + cw / 2 - xw / 2, 0, image.w - xw), xy = std::clamp(best[1] + ch / 2 - xh / 2, 0, image.h - xh);
    context = {xx, xy, xw, xh};
    return best;
}

void set_picture(const fs::path& source, const fs::path& result = {}) {
    try {
        if (app.picture) SDL_DestroyTexture(app.picture);
        if (app.result) SDL_DestroyTexture(app.result);
        app.picture = app.result = nullptr;
        auto image = load_png(source, 4);
        std::array<int, 4> context;
        auto detail = detail_region(image, context);
        app.picture = texture(crop(image, context[0], context[1], context[2], context[3]));
        app.focus = {float(detail[0] - context[0]), float(detail[1] - context[1]), float(detail[2]), float(detail[3])};
        if (!result.empty() && fs::exists(result)) {
            auto atlas = load_png(result, 4);
            int w = context[2] * 3, h = context[3] * 3, x = 2 + context[0] * 3, y = 2 + context[1] * 3;
            if (x + w <= atlas.w && y + h <= atlas.h) app.result = texture(crop(atlas, x, y, w, h), true);
        }
        app.picture_at = SDL_GetTicks();
    } catch (...) { app.picture = app.result = nullptr; }
}

void start_checks() {
    app.page = 1;
    app.checks.clear();
    app.checking = true;
    fs::path image = app.use_drive ? fs::path() : fs::u8path(app.image);
    std::string drive = app.use_drive && !app.drives.empty() ? app.drives[app.drive].letter : "";
    fs::path output = fs::u8path(app.output), archive = fs::u8path(app.archive);
    std::thread([=] {
        run_preflight(image, drive, output, archive, [](const Check& c) { std::lock_guard g(app.lock); app.checks.push_back(c); });
        std::lock_guard g(app.lock);
        app.checking = false;
    }).detach();
}

void add_log(const std::string& line) {
    bool detail = line.rfind("[detail] ", 0) == 0;
    auto now = std::time(nullptr);
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S ", std::localtime(&now));
    app.log.push_back({detail ? "  " + line.substr(9) : stamp + line, detail});
}

void set_stage(int index) {
    if (index == app.stage) return;
    app.stage = index;
    app.fraction = 0;
    app.recent.clear();
    app.marquee = true;
    app.caption = app.count = app.speed = app.eta = "";
    app.stage_started = std::chrono::steady_clock::now();
    if (app.picture) { SDL_DestroyTexture(app.picture); app.picture = nullptr; }
    if (app.result) { SDL_DestroyTexture(app.result); app.result = nullptr; }
    if (STAGES[index].key == std::string("app")) {
        auto frame = app.work / "frames" / "C1-VSTAV.FLC" / "frame-15.png";  // keep the hero before cleanup
        if (!app.character && fs::exists(frame)) app.character = texture(load_png(frame, 4));
    }
}

void progress(double fraction, const std::string& count, const std::string& caption) {
    app.fraction = fraction;
    app.marquee = false;
    if (!count.empty()) app.count = count;
    if (!caption.empty()) app.caption = caption;
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - app.stage_started).count();
    if (fraction > 0.02 && seconds > 5) {
        int left = int(seconds * (1 - fraction) / fraction / 60 + 0.5);
        app.eta = left < 1 ? "méně než minuta" : std::to_string(left) + " min";
    }
}

void handle(const std::string& line) {
    if (line.rfind("[stage] ", 0) == 0) {
        int i = stage_index(line.substr(8));
        if (i >= 0) set_stage(i);
        return;
    }
    if (line.rfind("[progress] ", 0) == 0) {
        char kind[16]{}, name[1024]{};
        int done = 0, total = 1;
        if (std::sscanf(line.c_str() + 11, "%15s %d %d %1023[^\n]", kind, &done, &total, name) < 3) return;
        double minutes = std::chrono::duration<double>(std::chrono::steady_clock::now() - app.stage_started).count() / 60;
        if (minutes > 0.05) { char s[32]; std::snprintf(s, sizeof s, "%.1f obr./min", done / minutes); app.speed = s; }
        std::string n = name;
        if (app.recent.empty() || app.recent.back() != n) app.recent.push_back(fs::u8path(n).filename().string());
        if (std::string(kind) == "decrypt") {
            fs::path path = fs::u8path(n);
            progress(double(done) / total, std::to_string(done) + " / " + std::to_string(total), path.stem().string());
            if (n != app.shown) {
                app.shown = n;
                try {
                    app.thumbs.push_back(thumbnail(path));
                    if (app.thumbs.size() > 12) { SDL_DestroyTexture(app.thumbs.front().texture); app.thumbs.pop_front(); }
                    app.picture_at = SDL_GetTicks();
                } catch (...) {}
            }
        } else {
            progress(double(done) / total, std::to_string(done) + " / " + std::to_string(total), n);
            if (n != app.shown) {
                app.shown = n;
                auto pack = std::string(kind) == "upscale" ? "original" : "remastered";
                auto folder = app.work / "packs" / pack / "flc" / n;
                fs::path result = folder / "overlay-0.png";
                app.next_source = app.work / "frames" / n / "frame-0.png";
                app.next_result = result;
                if (!app.picture) { set_picture(app.next_source, app.next_result); app.next_source.clear(); }
            }
        }
        return;
    }
    add_log(line);
    int done = 0, total = 0, percent = 0;
    if (std::sscanf(line.c_str(), "Čtu CD data: %d/%d", &done, &total) == 2 && total) progress(double(done) / total, std::to_string(done) + " / " + std::to_string(total), "");
    auto at = line.find("Download ");
    if (at != std::string::npos && std::sscanf(line.c_str() + at, "Download %d%%", &percent) == 1) progress(percent / 100.0, std::to_string(percent) + " %", "art.zip");
}

void watch_extract() {
    if (!app.busy || app.stage != 1 || SDL_GetTicks() - app.last_watch < 500) return;
    app.last_watch = SDL_GetTicks();
    std::error_code ec;
    auto folder = app.work / "frames";
    if (!fs::is_directory(folder, ec)) return;
    int count = 0;
    fs::path newest;
    fs::file_time_type best{};
    for (auto& e : fs::directory_iterator(folder, ec)) {
        if (!e.is_directory(ec)) continue;
        ++count;
        auto t = e.last_write_time(ec);
        if (t > best) { best = t; newest = e.path(); }
    }
    if (!count) return;
    progress(std::min(1.0, count / 595.0), std::to_string(count) + " / 595", newest.filename().string());
    if (app.recent.empty() || app.recent.back() != newest.filename().string()) app.recent.push_back(newest.filename().string());
    if (newest.filename().string() != app.shown && fs::exists(newest / "frame-0.png")) { app.shown = newest.filename().string(); set_picture(newest / "frame-0.png"); }
}

void remember_session(bool force = false);

void start_build() {
    app.page = 2;
    app.busy = true; app.failed = false; app.details = false; app.error.clear();
    app.stage = -1; set_stage(0);
    app.started = std::chrono::steady_clock::now();
    BuildRequest request;
    if (app.use_drive && !app.drives.empty()) request.drive = app.drives[app.drive].letter;
    else request.image = fs::u8path(app.image);
    request.output = fs::u8path(app.output);
    request.art_archive = app.archive.empty() ? fs::path() : fs::u8path(app.archive);
    request.cleanup = app.cleanup;
    app.work = request.output / ".builder";
    bool same = app.resume && app.resume->output == app.output;
    app.session = Session{request.drive.empty() ? app.image : "", request.drive, app.output, app.archive, app.cleanup, 0, 0,
                          same ? app.resume->started : int64_t(std::time(nullptr)), int64_t(std::time(nullptr))};
    app.resume.reset();
    app.attention = false;
    remember_session(true);
    if (app.worker.joinable()) app.worker.join();
    app.worker = std::thread([request] {
        auto emit = [](const std::string& line) { std::lock_guard g(app.lock); app.messages.push_back(line); };
        try {
            auto result = build_game(request, emit);
            emit("[result] " + result.string());
        } catch (const std::exception& error) {
            emit(std::string("[error] ") + error.what());
        }
    });
}

void open_with_system(const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    for (auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    if (auto* p = SDL_CreateProcess(argv.data(), false)) SDL_DestroyProcess(p);
}

void launch_game() {
#ifdef _WIN32
    open_with_system({(app.result_path / "HorkeLeto.exe").string()});
#else
    open_with_system({"/usr/bin/open", app.result_path.string()});
#endif
}

void reveal() {
#ifdef _WIN32
    open_with_system({"explorer", "/select," + app.result_path.string()});
#else
    open_with_system({"/usr/bin/open", "-R", app.result_path.string()});
#endif
}

// ---- file dialogs (callbacks may arrive on another thread) ----
void SDLCALL dialog_done(void* which, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::lock_guard g(app.lock);
    app.pending_paths.push_back({int(intptr_t(which)), files[0]});
}

void choose_image() {
    static const SDL_DialogFileFilter filters[] = {{"Obrazy CD", "cue;bin;img;ccd;mdf;mds;nrg;iso;chd"}, {"Všechny soubory", "*"}};
    SDL_ShowOpenFileDialog(dialog_done, (void*)0, app.window, filters, 2, nullptr, false);
}
void choose_output() { SDL_ShowOpenFolderDialog(dialog_done, (void*)1, app.window, app.output.c_str(), false); }
void choose_archive() {
    static const SDL_DialogFileFilter filters[] = {{"Archiv ZIP", "zip"}};
    SDL_ShowOpenFileDialog(dialog_done, (void*)2, app.window, filters, 1, nullptr, false);
}

// ---- pages (layout follows the approved mockup, design D) ----
const SDL_Color INK{20, 20, 20, 255}, NOTE{70, 70, 70, 255}, ACCENT{0, 0, 160, 255}, OKC{0, 128, 0, 255}, WARNC{160, 120, 0, 255}, FAILC{192, 0, 0, 255};
const char* EYEBROW[] = {"ČTENÍ CD", "GRP / FLC", "STAŽENÍ", "DEŠIFROVÁNÍ", "UPSCALING 3X", "REMASTER", "SESTAVENÍ"};
constexpr float CX = 0, CY = 46, CW = W, CH = H - 46 - 84;  // content area between tabs and footer

void eyebrow(float x, float y, const std::string& s) { text(tiny, x, y, s, ACCENT); }

void hatch(float x, float y, float w, float h) {
    fill(x, y, w, h, FACE);
    for (float yy = y; yy < y + h; yy += 2) for (float xx = x + (int(yy) % 4 ? 0 : 1); xx < x + w; xx += 2) fill(xx, yy, 1, 1, SDL_Color{176, 176, 176, 255});
    color(LIGHT); SDL_FRect r{x, y, w, h}; SDL_RenderRect(R, &r);
    color(SHADOW); SDL_FRect r2{x + 1, y + 1, w - 2, h - 2}; SDL_RenderRect(R, &r2);
}

void chrome() {
    const char* tabs[] = {"Originální herní data", "Kontrola", "Remaster", "Hotovo"};
    float tw = (W - 12) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        float x = 6 + i * tw, y = 6;
        bool active = i == app.page;
        fill(x, y, tw - 4, 40, active ? SDL_Color{208, 208, 208, 255} : FACE);
        bevel(x, y, tw - 4, 40);
        std::string number = "0" + std::to_string(i + 1);
        float total = text_width(mono, number) + 8 + text_width(regular, tabs[i]);
        float lx = x + (tw - 4 - total) / 2;
        text(mono, lx, y + 13, number, NOTE);
        text(regular, lx + text_width(mono, number) + 8, y + 13, tabs[i], active ? INK : NOTE);
        if (active) fill(x + 10, y + 34, tw - 24, 1, NAVY);
    }
    fill(0, H - 84, W, 1, SHADOW); fill(0, H - 83, W, 1, LIGHT);
    // "+ Zobrazit podrobnosti" strip
    float sy = H - 82;
    bool hot = inside(0, sy, W, 30);
    text(regular, 20, sy + 8, app.details ? "−  Skrýt podrobnosti" : "+  Zobrazit podrobnosti", INK);
    text(tiny, W - 20 - text_width(tiny, "PODROBNÝ PROTOKOL"), sy + 10, "PODROBNÝ PROTOKOL", NOTE);
    if (hot && in.released) app.details = !app.details;
    fill(0, H - 52, W, 1, SHADOW); fill(0, H - 51, W, 1, LIGHT);
}

void log_drawer() {
    float x = 16, y = CY + 6, w = W - 32, h = CH - 12;
    fill(x, y, w, h, FACE);
    bevel(x, y, w, h);
    title_bar(x + 3, y + 3, w - 6, 22, "Podrobný protokol", icon("document-16"));
    float lx = x + 10, ly = y + 34, lw = w - 20, lh = h - 80;
    fill(lx, ly, lw, lh, LIGHT);
    bevel(lx, ly, lw, lh, true);
    std::vector<const LogLine*> lines;
    for (auto& l : app.log) if (app.everything || !l.detail) lines.push_back(&l);
    int visible = int(lh / 15) - 1;
    if (inside(lx, ly, lw, lh)) app.log_scroll = std::clamp(app.log_scroll - in.wheel * 3, 0.f, float(std::max(0, int(lines.size()) - visible)));
    int first = std::max(0, int(lines.size()) - visible - int(app.log_scroll));
    for (int i = 0; i < visible && first + i < int(lines.size()); ++i)
        text(mono, lx + 6, ly + 4 + i * 15, elide(mono, lines[first + i]->text, lw - 12), lines[first + i]->detail ? NOTE : INK);
    if (lines.empty()) text(regular, lx + 8, ly + 8, "Zatím tu nic není. Protokol se plní během kontroly a sestavení.", NOTE);
    if (button(lx, y + h - 38, 150, 26, app.everything ? "Úroveň: všechno" : "Úroveň: důležité")) app.everything = !app.everything;
    if (button(lx + 160, y + h - 38, 110, 26, "Kopírovat")) {
        std::string all;
        for (auto& l : app.log) all += l.text + "\n";
        SDL_SetClipboardText(all.c_str());
    }
}

bool tile_option(float x, float y, float w, float h, const char* glyph, const std::string& label, const std::string& sub, bool selected, bool enabled) {
    fill(x, y, w, h, selected ? SDL_Color{220, 220, 220, 255} : FACE);
    bevel(x, y, w, h, selected);
    if (selected) { color(DARK); SDL_FRect r{x + 3, y + 3, w - 6, h - 6}; SDL_RenderRect(R, &r); }
    draw(icon(glyph), x + 18, y + (h - 32) / 2, 32, 32);
    text(regular, x + 62, y + h / 2 - 16, label, enabled ? INK : DISABLED);
    text(tiny, x + 62, y + h / 2 + 4, sub, enabled ? (selected ? ACCENT : NOTE) : DISABLED);
    return enabled && inside(x, y, w, h) && in.released;
}

void page_source() {
    hatch(24, CY + 56, 240, 352);
    draw(icon("cdr-192"), 48, CY + 112, 192, 192);
    float x = 292, w = W - x - 24, y = CY + 56;
    eyebrow(x, y, "HORKÉ LÉTO 1");
    text(heading, x, y + 16, "Vytáhni svoje cédečko. Nebo jeho CD image soubor.");
    text(regular, x, y + 50, "Vyber obraz svého CD. Ověříme data a sestavíme remaster.", NOTE);
    float tw = (w - 12) / 2;
    if (tile_option(x, y + 72, tw, 72, "document-32", "CD image", app.use_drive ? "OBRAZ NA DISKU" : "VYBRÁNO", !app.use_drive, true)) app.use_drive = false;
#ifdef _WIN32
    bool drives = !app.drives.empty();
    if (tile_option(x + tw + 12, y + 72, tw, 72, "disc-32", "CD mechanika", drives ? (app.use_drive ? "VYBRÁNO" : "Z MECHANIKY") : "ŽÁDNÁ MECHANIKA", app.use_drive, drives)) app.use_drive = true;
#else
    tile_option(x + tw + 12, y + 72, tw, 72, "disc-32", "CD mechanika", "Jen ve Windows", false, false);
    text(regular, x, y + 152, "Fyzická CD mechanika je dostupná jen ve Windows. Na macOS použij CD image.", NOTE);
#endif
    float ry = y + 176;
    if (!app.use_drive) {
        const char* pick = "Vybrat CD image...";
        float pw = text_width(regular, pick) + 44;  // folder icon + padding; grows with the text
        if (button(x, ry, pw, 26, std::string("     ") + pick)) choose_image();
        draw(icon("folder-16"), x + 8, ry + 5, 16, 16);
        text(mono, x + pw + 12, ry + 6, elide(mono, app.image.empty() ? "nic nevybráno" : fs::u8path(app.image).filename().string(), w - pw - 18), app.image.empty() ? NOTE : INK);
        if (!app.detected.empty()) {
            draw(icon("document-16"), x, ry + 34, 16, 16);
            text(regular, x + 22, ry + 34, app.detected, INK);
        }
        text(regular, x, ry + 56, "Podporované formáty: BIN/CUE, IMG/CCD, MDF/MDS nebo NRG.", NOTE);
    } else {
        text(regular, x, ry + 6, "Mechanika:", INK);
        std::string label = app.drives.empty() ? "-" : app.drives[app.drive].letter + "  " + app.drives[app.drive].label + (app.drives.size() > 1 ? "   ▼" : "");
        field(x + 90, ry + 1, 220, label);
        if (app.drives.size() > 1 && inside(x + 90, ry + 1, 220, 24) && in.released) app.drive = (app.drive + 1) % int(app.drives.size());
        text(regular, x, ry + 56, "Vlož originální CD. Data i hudbu přečteme přímo z něj.", NOTE);
    }
    float fy = y + 254;
    fill(x, fy, w, 1, SHADOW); fill(x, fy + 1, w, 1, LIGHT);
    draw(icon("folder-16"), x, fy + 18, 16, 16);
    text(tiny, x + 26, fy + 12, "CÍLOVÁ SLOŽKA", NOTE);
    text(mono, x + 26, fy + 26, elide(mono, app.output, w - 140), INK);
    if (button(x + w - 92, fy + 12, 92, 28, "Změnit")) choose_output();
    fill(x, fy + 52, w, 1, SHADOW); fill(x, fy + 53, w, 1, LIGHT);
    if (checkbox(x, fy + 66, "Po skončení odstranit dočasné soubory", app.cleanup)) app.cleanup = !app.cleanup;
    text(regular, x, fy + 92, "Potřebuješ asi 45 GB volného místa pro dočasné soubory. Výsledná hotová hra zabere asi 17 GB.", NOTE);
    bool ready = app.use_drive || !app.image.empty();
    if (button(W - 230, H - 44, 210, 32, "Další", ready, true) || (in.enter && ready)) start_checks();
}

std::string when(int64_t seconds) {
    std::time_t t = std::time_t(seconds);
    char out[32];
    std::strftime(out, sizeof out, "%d. %m. %Y %H:%M", std::localtime(&t));
    return out;
}

// Windows 98 style dialog over the first screen: continue the unfinished build with its old settings.
void resume_dialog() {
    auto& r = *app.resume;
    float w = 560, h = 292, x = (W - w) / 2, y = CY + (CH - h) / 2;
    fill(x + 4, y + 4, w, h, SDL_Color{0, 0, 0, 90});
    fill(x, y, w, h, FACE);
    bevel(x, y, w, h, false);
    title_bar(x + 3, y + 3, w - 6, 22, "Rozpracované sestavení", icon("disc-16"));
    float cx = x + 20, cy = y + 40;
    draw(icon("cdr-128"), cx, cy + 6, 72, 72);
    float tx = cx + 92, tw = w - (tx - x) - 20;
    text(bold, tx, cy, "Minule se sestavení nedokončilo.", INK);
    text(regular, tx, cy + 22, "Můžeš pokračovat se stejným nastavením.", NOTE);
    text(regular, tx, cy + 40, "Již hotové části se přeskočí.", NOTE);
    std::string source = r.drive.empty() ? fs::u8path(r.image).filename().string() : "mechanika " + r.drive;
    const std::pair<const char*, std::string> rows[] = {
        {"CD", source}, {"Cíl", r.output},
        {"Hotovo", "asi " + std::to_string(r.percent) + " %, krok " + std::to_string(r.stage + 1) + " ze 7"},
        {"Naposledy", when(r.updated ? r.updated : r.started)}};
    float ry = cy + 70;
    for (auto& [label, value] : rows) {
        text(tiny, tx, ry + 2, label, NOTE);
        text(mono, tx + 76, ry, elide(mono, value, tw - 76), INK);
        ry += 20;
    }
    if (!app.resume_error.empty()) text(regular, cx, y + h - 74, elide(regular, app.resume_error, w - 40), FAILC);
    float by = y + h - 44;
    bool go = button(x + w - 150, by, 130, 28, "Pokračovat", true, true) || in.enter || SDL_getenv("HB_TEST_RESUME_GO");  // dev hook
    bool change = button(x + w - 300, by, 140, 28, "Změnit nastavení");
    bool drop = button(x + 20, by, 150, 28, "Smazat rozdělané");
    if (change) {
        // Keep the old choices as a starting point; nothing is deleted.
        app.image = r.image; app.output = r.output; app.archive = r.archive; app.cleanup = r.cleanup;
        app.use_drive = !r.drive.empty();
        if (!r.image.empty()) try { app.detected = "Rozpoznáno: " + format_name(detect_format(fs::u8path(r.image))); } catch (...) {}
        app.resume.reset();
    } else if (drop) {
        const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Nechat"}, {0, 1, "Smazat"}};
        std::string message = "Smazat rozdělanou práci ve složce\n" + (fs::u8path(r.output) / ".builder").string() + "?\nPříště se začne od začátku.";
        SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING, app.window, "Smazat rozdělané", message.c_str(), 2, buttons, nullptr};
        int choice = 0;
        SDL_ShowMessageBox(&box, &choice);
        if (choice == 1) {
            auto folder = fs::u8path(r.output) / ".builder";
            clear_session();
            app.resume.reset();
            std::thread([folder] { try { remove_tree(folder); } catch (...) {} }).detach();
        }
    } else if (go) {
        app.resume_error.clear();
        if (!r.drive.empty()) {
            auto it = std::find_if(app.drives.begin(), app.drives.end(), [&](const Drive& d) { return d.letter == r.drive; });
            if (it == app.drives.end()) { app.resume_error = "Mechanika " + r.drive + " teď není k dispozici. Vlož CD a zkus to znovu."; return; }
            app.use_drive = true; app.drive = int(it - app.drives.begin());
        } else {
            if (!fs::exists(fs::u8path(r.image))) { app.resume_error = "Obraz CD už na původním místě není. Vyber ho přes Změnit nastavení."; return; }
            app.use_drive = false; app.image = r.image;
        }
        if (!r.archive.empty() && !fs::exists(fs::u8path(r.archive))) { app.resume_error = "Archiv s remaster daty už na původním místě není."; return; }
        app.output = r.output; app.archive = r.archive; app.cleanup = r.cleanup;
        start_build();
    }
}

void start_update_download();

void offer_game_update(const fs::path& app_path) {
    auto info = inspect_game(app_path);
    if (!info) return;
    app.game_info = *info;
    app.game = App::GameUpdate::Offer;
    app.game_at = SDL_GetTicks();
}

void find_outdated_games() {
    if (SDL_getenv("HB_DEMO")) return;
    std::thread([] {
        for (auto& path : known_games()) {
            auto info = inspect_game(path);
            if (!info || !info->outdated) continue;
            std::lock_guard g(app.lock);
            app.messages.push_back("[game-outdated] " + path.string());
        }
    }).detach();
}

void start_game_update() {
    app.game = App::GameUpdate::Running;
    app.game_at = SDL_GetTicks();
    app.game_error.clear();
    app.game_status = "Připravuji nový engine...";
    if (app.game_worker.joinable()) app.game_worker.join();
    app.game_worker = std::thread([target = app.game_info.app] {
        auto emit = [](const std::string& line) {
            std::lock_guard g(app.lock);
            app.messages.push_back("[game] " + (line.rfind("[detail] ", 0) == 0 ? line.substr(9) : line));
        };
        try {
            update_game(target, emit);
            std::lock_guard g(app.lock);
            app.messages.push_back("[game-done]");
        } catch (const std::exception& e) {
            std::lock_guard g(app.lock);
            app.messages.push_back(std::string("[game-error] ") + e.what());
        }
    });
}

// "Aktualizovat hru": swap the engine of a game built earlier; the mechanic tightens a bolt meanwhile.
void game_update_dialog() {
    double t = (SDL_GetTicks() - app.game_at) / 1000.0;
    float w = 560, h = 286, x = (W - w) / 2, y = CY + (CH - h) / 2;
    fill(x + 4, y + 4, w, h, SDL_Color{0, 0, 0, 90});
    fill(x, y, w, h, FACE);
    bevel(x, y, w, h, false);
    title_bar(x + 3, y + 3, w - 6, 22, "Aktualizace hry", icon("disc-16"));
    float cx = x + 20, cy = y + 40;
    bool done = app.game == App::GameUpdate::Done, running = app.game == App::GameUpdate::Running;
    // The burned CD spins; ones and zeroes stream out of it to the right (fast while the engine is swapped).
    const float cd = 64, ccx = cx + cd / 2, ccy = cy + 10 + cd / 2;
    double speed = running ? 1.0 : done ? 0.0 : 0.25;
    if (speed > 0) {
        const int streams = 14;
        for (int k = 0; k < streams; ++k) {
            double flight = t * (0.55 + 0.6 * speed) + k * 0.61803, p = flight - std::floor(flight);
            float dx = ccx + 18 + float(p) * 130, dy = ccy - 18 + float((k * 37) % 36) + float(std::sin(p * 6 + k) * 3);
            Uint8 alpha = Uint8(255 * (1 - p) * (running ? 1.0 : 0.6));
            uint32_t h = uint32_t(k + 1) * 2654435761u ^ uint32_t(std::floor(flight)) * 40503u;  // new digit every flight
            const char* bit = (h >> 13) & 1 ? "1" : "0";
            SDL_Color c = (k % 3) ? SDL_Color{0, 0, 128, alpha} : SDL_Color{0, 128, 0, alpha};
            text(p < 0.5 ? mono : tiny, dx, dy, bit, c);
        }
    }
    if (done) draw(icon("check-32"), cx + 8, cy + 18, 48, 48);
    else draw(icon("cdr-128"), cx, cy + 10, cd, cd, std::fmod(t * 360 * (running ? 1.5 : 0.15), 360.0));
    float tx = cx + 200, tw = w - (tx - x) - 20;
    std::string built = app.game_info.builder_version.empty() ? "1.0.0" : app.game_info.builder_version;
    switch (app.game) {
    case App::GameUpdate::Offer:
        text(bold, tx, cy, app.game_info.outdated_engine || app.game_info.missing_variants ? "Tvoje hra má starší engine." : "Tvoje hra má starší obrázky.", INK);
        if (app.game_info.missing_variants) {
            text(regular, tx, cy + 22, "Vyměníme engine a doplníme " + std::to_string(app.game_info.missing_variants), NOTE);
            text(regular, tx, cy + 40, "obrázků pro 4. a 5. kapitolu.", NOTE);
            text(regular, tx, cy + 58, "Pozice zůstanou, asi 15 minut.", NOTE);
            break;
        }
        if (!app.game_info.outdated_engine) {
            text(regular, tx, cy + 22, "Doplníme překreslené obrázky pro místnosti", NOTE);
            text(regular, tx, cy + 40, "s jinou paletou. Pozice zůstanou.", NOTE);
            text(regular, tx, cy + 58, "Zabere to pár minut.", NOTE);
            break;
        }
        text(regular, tx, cy + 22, "Vyměníme jen engine. Obrázky, hudba", NOTE);
        text(regular, tx, cy + 40, "i uložené pozice zůstanou původní.", NOTE);
#ifdef _WIN32
        text(regular, tx, cy + 58, "Zabere to jen pár vteřin.", NOTE);
#else
        text(regular, tx, cy + 58, "Zabere to minutu nebo dvě.", NOTE);
#endif
        break;
    case App::GameUpdate::Running:
        text(bold, tx, cy, app.game_info.outdated_engine || app.game_info.missing_variants ? "Měníme engine..." : "Doplňujeme obrázky...", INK);
        text(regular, tx, cy + 22, elide(regular, app.game_status, tw), NOTE);
        segments(tx, cy + 46, tw, 18, 0, int(t * 12));
        break;
    case App::GameUpdate::Done:
        text(bold, tx, cy, "Hotovo. Hra má nejnovější engine.", INK);
        text(regular, tx, cy + 22, "Teď už to můžeš pokazit jen hraním.", NOTE);
        text(regular, tx, cy + 40, "Zase.", NOTE);
        break;
    default:
        text(bold, tx, cy, "Aktualizace se nepovedla.", FAILC);
        text(regular, tx, cy + 22, elide(regular, app.game_error, tw), NOTE);
        text(regular, tx, cy + 40, "Hra zůstala, jak byla.", NOTE);
        break;
    }
    const std::pair<const char*, std::string> rows[] = {{"Hra", app.game_info.app.string()},
        {"Engine", std::string("z průvodce ") + built + "  >  " + builder_version()}};
    float ry = cy + 104, rx = cx;
    for (auto& [label, value] : rows) {
        text(tiny, rx, ry + 2, label, NOTE);
        text(mono, rx + 60, ry, elide(mono, value, w - 40 - 60), INK);
        ry += 20;
    }
    float by = y + h - 44;
    auto next = [] {
        if (app.outdated.empty()) { app.game = App::GameUpdate::None; return; }
        app.game_info = app.outdated.front(); app.outdated.pop_front(); app.game = App::GameUpdate::Offer; app.game_at = SDL_GetTicks();
    };
    if (app.game == App::GameUpdate::Offer) {
        if (button(x + w - 170, by, 150, 28, "Aktualizovat hru", true, true) || in.enter) start_game_update();
        if (button(x + w - 290, by, 110, 28, "Teď ne")) next();
    } else if (done) {
        if (button(x + w - 150, by, 130, 28, "Spustit hru", true, true)) { app.result_path = app.game_info.app; launch_game(); next(); }
        if (button(x + w - 260, by, 100, 28, "Zavřít")) next();
    } else if (app.game == App::GameUpdate::Failed) {
        if (button(x + w - 130, by, 110, 28, "Zavřít")) next();
    }
}

void start_update_check() {
    if (SDL_getenv("HB_DEMO") && !SDL_getenv("HB_UPDATE_API")) return;
    if (!app.replaced.empty()) return;  // just updated: never chain into another update in the same session
    app.update_worker = std::thread([] {
        auto found = check_for_update();
        if (!found) return;
        std::lock_guard g(app.lock);
        app.update_info = *found;
        app.messages.push_back("[upd-found]");
    });
}

void start_update_download() {
    app.update = App::Update::Downloading;
    app.update_at = SDL_GetTicks();
    app.update_fraction = 0;
    app.update_error.clear();
    if (app.update_worker.joinable()) app.update_worker.join();
    app.update_worker = std::thread([info = app.update_info] {
        auto emit = [](const std::string& line) { std::lock_guard g(app.lock); app.messages.push_back("[upd] " + line); };
        try {
            auto file = download_update(info, emit);
            std::lock_guard g(app.lock);
            app.messages.push_back("[upd-done] " + file.string());
        } catch (const std::exception& e) {
            std::lock_guard g(app.lock);
            app.messages.push_back(std::string("[upd-error] ") + (cancelled ? "Stahování zrušeno." : e.what()));
        }
    });
}

// Pixel globe that spins: the "internet" end of the download animation.
void globe(float cx, float cy, float r, double t) {
    const float px = 2;
    for (float y = -r; y < r; y += px)
        for (float x = -r; x < r; x += px) {
            float d = (x * x + y * y) / (r * r);
            if (d > 1) continue;
            // Longitude spins with time; a few blobby continents.
            double lon = std::asin(std::clamp(x / std::sqrt(std::max(1e-3f, r * r - y * y)), -1.0f, 1.0f)) + t * 1.4;
            double lat = std::asin(y / r);
            double land = std::sin(lon * 3) * std::cos(lat * 4) + std::sin(lon * 5 + 1.3) * 0.5;
            SDL_Color c = land > 0.55 ? SDL_Color{0, 160, 0, 255} : SDL_Color{0, 0, 168, 255};
            if (d > 0.82 && land <= 0.55) c = SDL_Color{0, 0, 128, 255};
            fill(cx + x, cy + y, px, px, c);
        }
    // Grid lines like the Windows 98 globe icon.
    for (int k = -1; k <= 1; ++k) line(cx - r * std::sqrt(1 - k * k * 0.25f), cy + k * r / 2, cx + r * std::sqrt(1 - k * k * 0.25f), cy + k * r / 2, SDL_Color{96, 160, 255, 255});
}

void update_toast() {
    bool updated = !app.replaced.empty();
    if (!updated && (app.update != App::Update::Found || app.update_dismissed || app.busy || app.resume)) return;
    if (updated && SDL_GetTicks() - app.update_at > 7000) { app.replaced.clear(); return; }
    double t = (SDL_GetTicks() - app.update_at) / 1000.0;
    float w = 356, h = 52, x = 14;
    double slide = std::min(1.0, t / 0.35);
    float y = H - 4 - h * float(1 - (1 - slide) * (1 - slide));
    fill(x, y, w, h, FACE);
    bevel(x, y, w, h, false);
    // The floppy with the new version drops in and bounces.
    double d = std::clamp((t - 0.25) / 0.9, 0.0, 1.0);
    double bounce = d < 0.6 ? 1 - (d / 0.6) * (d / 0.6) : std::abs(std::sin((d - 0.6) / 0.4 * 3.14159)) * 0.18 * (1 - d);
    float iy = y + 10 - float(bounce) * 70;
    draw(icon(updated ? "check-32" : "floppy-32"), x + 10, iy, 32, 32);
    if (d >= 1 && !updated)
        for (int k = 0; k < 4; ++k) {
            double phase = std::fmod(t * 2.2 + k * 0.37, 1.0);
            if (phase > 0.5) continue;
            float sx = x + 6 + float((k * 29) % 40), sy = y + 4 + float((k * 17) % 34), a = float(phase * 6);
            SDL_Color star{255, 236, 64, 255};
            line(sx - a, sy, sx + a, sy, star); line(sx, sy - a, sx, sy + a, star);
        }
    if (updated) {
        text(bold, x + 52, y + 9, std::string("Aktualizováno na verzi ") + builder_version(), INK);
        text(small, x + 52, y + 28, "Předtím " + app.replaced + ". Teď už to můžeš pokazit novější verzí.", NOTE);
        return;
    }
    text(bold, x + 52, y + 9, "Nová verze " + app.update_info.version + "!", INK);
    text(small, x + 52, y + 28, std::string("Máš ") + builder_version() + ". Aktualizovat stáhne a spustí novou verzi.", NOTE);
    if (button(x + w - 110, y + 12, 100, 26, "Aktualizovat", true, true)) start_update_download();
    if (inside(x + w - 18, y + 2, 16, 12) && in.released) app.update_dismissed = true;
    text(tiny, x + w - 14, y + 2, "x", NOTE);
}

// Windows 98 style download dialog: pages fly from the spinning globe into the folder.
void update_dialog() {
    double t = (SDL_GetTicks() - app.update_at) / 1000.0;
    float w = 540, h = 268, x = (W - w) / 2, y = CY + (CH - h) / 2;
    fill(x + 4, y + 4, w, h, SDL_Color{0, 0, 0, 90});
    fill(x, y, w, h, FACE);
    bevel(x, y, w, h, false);
    title_bar(x + 3, y + 3, w - 6, 22, "Aktualizace průvodce", icon("floppy-16"));
    float gx = x + 70, fx = x + w - 110, ay = y + 82;
    globe(gx, ay, 26, SDL_GetTicks() / 1000.0);
    text(tiny, gx - text_width(tiny, "GITHUB") / 2, ay + 32, "GITHUB", INK);
#ifdef _WIN32
    const char* into = "SLOŽKA PRŮVODCE";
#else
    const char* into = "STAŽENÉ SOUBORY";
#endif
    draw(icon("folder-32"), fx - 20, ay - 20, 40, 40);
    text(tiny, fx - text_width(tiny, into) / 2, ay + 32, into, INK);
    bool moving = app.update == App::Update::Downloading;
    if (moving)
        for (int k = 0; k < 3; ++k) {
            double p = std::fmod(t * 0.9 + k / 3.0, 1.0);
            float px = gx + 30 + float(p) * (fx - gx - 60), py = ay - 8 - float(std::sin(p * 3.14159) * 34);
            draw(icon("document-16"), px - 8, py - 8, 16, 16, p * 40 - 20);
        }
    float ty = y + 140;
    char line_text[160];
    if (app.update == App::Update::Failed) {
        text(bold, x + 20, ty, "Aktualizace se nepovedla.", FAILC);
        text(regular, x + 20, ty + 22, elide(regular, app.update_error, w - 40), NOTE);
    } else if (app.update == App::Update::Starting) {
        text(bold, x + 20, ty, "Staženo a ověřeno. Spouštím verzi " + app.update_info.version + "...", INK);
        text(regular, x + 20, ty + 22, "Tohle okno se za chvilku zavře samo.", NOTE);
    } else {
        double mb = app.update_info.bytes / 1048576.0;
        std::snprintf(line_text, sizeof line_text, "Stahuji verzi %s: %.1f MB z %.1f MB", app.update_info.version.c_str(), mb * app.update_fraction, mb);
        text(bold, x + 20, ty, line_text, INK);
        text(regular, x + 20, ty + 22, "Po stažení se ověří kontrolní součet.", NOTE);
    }
    segments(x + 20, ty + 46, w - 40, 20, app.update == App::Update::Starting ? 1.0 : app.update_fraction, -1);
    float by = y + h - 42;
    if (app.update == App::Update::Downloading) {
        if (button(x + w - 130, by, 110, 28, "Zrušit")) cancelled = true;
    } else if (app.update == App::Update::Failed) {
        if (button(x + w - 130, by, 110, 28, "Zavřít")) { app.update = App::Update::Found; app.update_dismissed = true; }
        if (button(x + w - 280, by, 140, 28, "Zkusit znovu", true, true)) start_update_download();
    } else if (app.update == App::Update::Starting && t > 0.8) {
        try {
            apply_update(app.update_file);
            app.quit = true;
        } catch (const std::exception& e) { app.update_error = e.what(); app.update = App::Update::Failed; }
    }
}

void page_checks() {
    std::vector<Check> rows;
    bool running;
    { std::lock_guard g(app.lock); rows = app.checks; running = app.checking; }
    int ok = 0, warn = 0, fail = 0;
    bool fix_archive = false, fix_output = false;
    for (auto& c : rows) {
        (c.state == Check::Ok ? ok : c.state == Check::Warn ? warn : fail)++;
        if (c.state == Check::Fail) { fix_archive |= c.fix == "archive"; fix_output |= c.fix == "output"; }
    }
    float x = 24, w = W - 48, y = CY + 14;
    eyebrow(x, y, "KONTROLA PŘED STARTEM");
    text(heading, x, y + 16, running ? "Proklepneme to..." : fail ? "Tady to chce nejdřív opravit." : warn ? "Půjde to. Ale má to háček." : "Vše v pořádku. Připraveno na sestavení remasteru.");
    std::string totals = running ? "OVĚŘUJEME" : std::to_string(ok) + " OK / " + std::to_string(warn) + " POZOR / " + std::to_string(fail) + " CHYBA";
    float bw = text_width(tiny, totals) + 20;
    fill(x + w - bw, y + 12, bw, 24, SDL_Color{224, 224, 224, 255}); bevel(x + w - bw, y + 12, bw, 24, true);
    text(tiny, x + w - bw + 10, y + 18, totals, INK);
    float ly = y + 52;
    fill(x, ly + 3, 8, 8, OKC); text(tiny, x + 12, ly, "V pořádku", INK);
    fill(x + 90, ly + 3, 8, 8, WARNC); text(tiny, x + 102, ly, "Upozornění", INK);
    fill(x + 190, ly + 3, 8, 8, FAILC); text(tiny, x + 202, ly, "Brání sestavení", INK);
    if (button(x + w - 150, ly - 6, 150, 22, "Zkontrolovat znovu", !running)) start_checks();
    float ty = ly + 24;
    title_bar(x, ty, w, 22, "Kontrola systému a herních dat", icon("magnifier-16"));
    float listy = ty + 22, listh = CY + CH - 34 - listy;
    fill(x, listy, w, listh, LIGHT);
    const std::map<std::string, const char*> glyphs{{"disc", "disc-16"}, {"space", "floppy-16"}, {"write", "folder-16"}, {"gpu", "gear-16"},
        {"network", "folder-16"}, {"art", "padlock-16"}, {"time", "gear-16"}};
    float ry = listy;
    for (auto& c : rows) {
        float rh = 36 + (c.hint.empty() ? 0 : 14);
        draw(icon(glyphs.count(c.key) ? glyphs.at(c.key) : "document-16"), x + 10, ry + 10, 16, 16);
        const char* mark = c.state == Check::Ok ? "[ OK ]" : c.state == Check::Warn ? "[POZOR]" : "[CHYBA]";
        text(mono, x + 40, ry + 10, mark, c.state == Check::Ok ? OKC : c.state == Check::Warn ? WARNC : FAILC);
        text(bold, x + 112, ry + 4, c.title, INK);
        text(small, x + 112, ry + 20, elide(small, c.text, w - 250), INK);
        if (!c.hint.empty()) text(small, x + 112, ry + 34, elide(small, "> " + c.hint, w - 250), ACCENT);
        if (c.state == Check::Fail && c.fix == "archive" && button(x + w - 130, ry + 6, 120, 24, "Vybrat archiv")) choose_archive();
        if (c.state == Check::Fail && c.fix == "output" && button(x + w - 130, ry + 6, 120, 24, "Jiná složka")) choose_output();
        if (c.state == Check::Fail && c.fix == "update" && button(x + w - 150, ry + 6, 140, 24, "Aktualizovat hru", true, true))
            offer_game_update(fs::u8path(app.output) / app_name());
        ry += rh;
        fill(x, ry - 1, w, 1, SDL_Color{224, 224, 224, 255});
    }
    if (running) {
        static const char* spin = "|/-\\";
        text(mono, x + 40, ry + 10, std::string("[ ČEKÁ ] ") + spin[(SDL_GetTicks() / 150) % 4] + "  Ověřujeme...", NOTE);
    }
    std::string conclusion = running ? "Čekáme na dokončení kontrol." : fail ? "Počet blokujících chyb: " + std::to_string(fail) + ". Oprav je, pak můžeš sestavit hru."
                                                                            : "Povinné kontroly prošly.";
    text(small, x, CY + CH - 26, conclusion, ACCENT);
    if (button(20, H - 44, 72, 32, "Zpět", !running)) app.page = 0;
    bool go = !running && !fail && !rows.empty();
    if (button(W - 150, H - 44, 130, 32, "Další", go, true) || (in.enter && go)) start_build();
}

void list_panel(float x, float y, float w, float h, const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows, const std::string& right_header) {
    fill(x, y, w, h, LIGHT);
    bevel(x, y, w, h, true);
    fill(x + 2, y + 2, w - 4, 20, NAVY);
    draw(icon("folder-16"), x + 6, y + 4, 16, 16);
    text(small, x + 28, y + 5, elide(small, title, w - 40), LIGHT);
    fill(x + 2, y + 22, w - 4, 16, SDL_Color{232, 232, 232, 255});
    text(tiny, x + 8, y + 24, "Název", NOTE);
    text(tiny, x + w - 10 - text_width(tiny, right_header), y + 24, right_header, NOTE);
    float ry = y + 42;
    for (auto& [name, value] : rows) {
        if (ry + 22 > y + h) break;
        draw(icon("document-16"), x + 8, ry + 2, 16, 16);
        text(mono, x + 30, ry + 2, elide(mono, name, w - 120), INK);
        text(tiny, x + w - 10 - text_width(tiny, value), ry + 4, value, ACCENT);
        ry += 24;
    }
}

// Character animations come from the player's exported frames (never from the builder).
struct Anim { std::vector<SDL_Texture*> frames; std::vector<SDL_FPoint> offsets; float w = 0, h = 0, ox = 0, oy = 0; double fps = 10; };
std::map<std::string, Anim> anims;
std::map<std::string, Uint64> anim_tried;

Anim* anim(const std::string& name) {
    auto it = anims.find(name);
    if (it != anims.end()) return &it->second;
    if (app.work.empty() || SDL_GetTicks() - anim_tried[name] < 1500) return nullptr;
    anim_tried[name] = SDL_GetTicks();
    try {
        auto manifest = read_json(app.work / "frames" / (name + ".json"));
        Anim a;
        float minx = 1e9f, miny = 1e9f, maxx = 0, maxy = 0;
        for (auto& f : manifest.at("frames")) {
            auto& c = f.at("crop");
            if (c.at("empty").get<bool>()) return nullptr;
            float x = c.at("x"), y = c.at("y"), w = c.at("width"), h = c.at("height");
            minx = std::min(minx, x); miny = std::min(miny, y); maxx = std::max(maxx, x + w); maxy = std::max(maxy, y + h);
            a.offsets.push_back({x, y});
        }
        for (auto& o : a.offsets) { o.x -= minx; o.y -= miny; }
        a.w = maxx - minx; a.h = maxy - miny; a.ox = minx; a.oy = miny;
        auto& delay = manifest.at("frames")[0].at("effective_delay");
        a.fps = 35.0 / std::max(1, delay.at("numerator").get<int>());
        for (size_t i = 0; i < a.offsets.size(); ++i)
            a.frames.push_back(texture(load_png(app.work / "frames" / name / ("frame-" + std::to_string(i) + ".png"), 4), true));
        return &(anims[name] = std::move(a));
    } catch (...) { return nullptr; }
}

// Draws frame `index` with its feet centred on (cx, ground); returns the top of the figure.
float draw_anim(Anim& a, double time, float cx, float ground, float scale) {
    size_t i = size_t(time * a.fps) % a.frames.size();
    float tw, th;
    SDL_GetTextureSize(a.frames[i], &tw, &th);
    float left = cx - a.w * scale / 2, top = ground - a.h * scale;
    draw(a.frames[i], left + a.offsets[i].x * scale, top + a.offsets[i].y * scale, tw * scale, th * scale);
    return top;
}

void shadowed(SDL_Texture* picture, float x, float y, float w, float h) {
    float tw, th;
    SDL_GetTextureSize(picture, &tw, &th);
    const SDL_FRect* part = picture == app.picture && app.focus.w > 0 ? &app.focus : nullptr;
    if (part) { tw = part->w; th = part->h; }
    float s = std::min(w / tw, h / th), dw = tw * s, dh = th * s;
    SDL_FRect shade{x + (w - dw) / 2 + 3, y + (h - dh) / 2 + 3, dw, dh};
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, 0, 0, 0, 70);
    SDL_RenderFillRect(R, &shade);
    SDL_FRect dst{x + (w - dw) / 2, y + (h - dh) / 2, dw, dh};
    SDL_RenderTexture(R, picture, part, &dst);
}

float app_right_edge = 0;
void stage_view(float x, float y, float w, float h) {
    int s = std::max(0, app.stage);
    std::string key = STAGES[s].key;
    double t = SDL_GetTicks() / 1000.0;
    if (key == "read") {  // small Windows 98 "Loading CD" dialog
        float dw = 300, dh = 220, dx = x + (w - dw) / 2, dy = y;
        fill(dx, dy, dw, dh, FACE);
        bevel(dx, dy, dw, dh);
        title_bar(dx + 3, dy + 3, dw - 6, 20, "Načítání CD...", icon("disc-16"));
        for (int i = 0; i < 2; ++i) { float bx = dx + dw - 42 + i * 18; fill(bx, dy + 6, 16, 14, FACE); bevel(bx, dy + 6, 16, 14); text(tiny, bx + 4, dy + 7, i ? "x" : "_", INK); }
        draw(icon("cdr-128"), dx + dw / 2 - 52, dy + 30, 104, 104, std::fmod(t * 240, 360));
        segments(dx + 20, dy + 144, dw - 40, 20, app.fraction, app.marquee ? int(t * 12) : -1);
        text(bold, dx + dw / 2 - text_width(bold, "Čteme disk...") / 2, dy + 172, "Čteme disk...", INK);
        text(tiny, dx + dw / 2 - text_width(tiny, "Obraz CD / data + 2 zvukové stopy") / 2, dy + 194, "Obraz CD / data + 2 zvukové stopy", NOTE);
        return;
    }
    fill(x, y, w, h, FACE);
    bevel(x, y, w, h);
    title_bar(x + 3, y + 3, w - 6, 20, std::string(STAGES[s].title) + "...", icon(STAGES[s].glyph));
    float bx = x + 12, by = y + 32, bw = w - 24, bh = h - 42;
    if (key == "decrypt") {
        float ground = by + bh - 6, cx = bx + 92;
        if (Anim* reader = anim("C10-CTE!.FLC")) {
            float top = draw_anim(*reader, t, cx, ground, 1.0f);
            // Hex characters lift off his sheet and drift into the grid.
            std::mt19937 rng(7);
            for (int k = 0; k < 14; ++k) {
                double phase = std::fmod(t * 0.45 + k / 14.0, 1.0);
                float sx = cx - 30 + float(rng() % 20), sy = top + 70 + float(rng() % 20);
                float ex = bx + 200 + float(rng() % 40), ey = by + 20 + float(rng() % 140);
                float px = float(sx + (ex - sx) * phase), py = float(sy + (ey - sy) * phase - std::sin(phase * 3.14159) * 30);
                char glyph[4]; std::snprintf(glyph, sizeof glyph, "%02X", unsigned((rng() + unsigned(t * 3)) & 255));
                text(tiny, px, py, glyph, SDL_Color{0, 0, 160, Uint8(255 * (1 - phase))});
            }
        } else {
            bool locked = SDL_GetTicks() - app.picture_at < 600;
            draw(icon(locked ? "padlock-32" : "unlocked-32"), cx - 28, by + 40, 56, 56);
        }
        text(tiny, cx - text_width(tiny, app.count) / 2, by + 2, app.count, NOTE);
        // Miniatures assemble from coloured hex glyphs, show briefly, then fade away. Purely sampled.
        if (!app.thumbs.empty()) {
            const double period = 3.6;
            const float mw = 80, mh = 60;
            float sheet_x = cx - 34, sheet_y = by + bh - 110;
            for (int slot = 0; slot < 3; ++slot) {
                double clock = t + slot * period / 3;
                long cycle = long(clock / period);
                double phase = std::fmod(clock, period) / period;
                auto& thumb = app.thumbs[size_t((cycle * 3 + slot) % long(app.thumbs.size()))];
                std::mt19937 seed(unsigned(cycle * 7 + slot));
                float mx = bx + 240 + slot * (mw + 70) + float(seed() % 40), my = by + 20 + float(seed() % 70);
                float drift = phase > 0.8 ? float((phase - 0.8) / 0.2 * 30) : 0;
                Uint8 fade = Uint8(255 * (phase > 0.8 ? 1 - (phase - 0.8) / 0.2 : 1));
                double gather = std::min(1.0, phase / 0.35), reveal = std::clamp((phase - 0.35) / 0.2, 0.0, 1.0);
                if (reveal < 1) {
                    std::mt19937 glyphs(unsigned(cycle * 131 + slot));
                    for (int gy = 0; gy < 9; ++gy)
                        for (int gx = 0; gx < 12; ++gx) {
                            SDL_Color c = thumb.cells[size_t(gy * 12 + gx)];
                            unsigned value = glyphs() & 255, delay = glyphs() % 100;
                            if (!c.a) continue;
                            double g = std::clamp((gather - delay / 300.0) / 0.67, 0.0, 1.0);
                            float tx = mx + gx * (mw / 12), ty = my + gy * (mh / 9) - drift;
                            float px = float(sheet_x + (tx - sheet_x) * g), py = float(sheet_y + (ty - sheet_y) * g - std::sin(g * 3.14159) * 25);
                            char hex[4]; std::snprintf(hex, sizeof hex, "%02X", value);
                            c.a = Uint8(fade * (1 - reveal) * (g > 0 ? 1 : 0));
                            text(tiny, px, py, hex, c);
                        }
                }
                if (reveal > 0) {
                    float tw, th;
                    SDL_GetTextureSize(thumb.texture, &tw, &th);
                    float s = std::min(mw / tw, mh / th);
                    SDL_SetTextureAlphaMod(thumb.texture, Uint8(fade * reveal));
                    draw(thumb.texture, mx + (mw - tw * s) / 2, my + (mh - th * s) / 2 - drift, tw * s, th * s);
                    SDL_SetTextureAlphaMod(thumb.texture, 255);
                }
            }
        }
    } else if (key == "upscale" || key == "remaster") {
        // The garage mechanic hammers the picture: every blow turns the next strip into the upscale.
        const int strikes = 6;
        Anim* smith = anim("C20-M-M!.FLC");
        double fps = smith ? smith->fps : 6;
        double beat = 6 / fps;
        double since = (SDL_GetTicks() - app.picture_at) / 1000.0;
        double work = std::max(0.0, since - 0.45);  // the hammer starts once the zoom has landed
        int hits = int(work / beat);
        if (hits > strikes + 1 && !app.next_source.empty()) {  // sequence done: take the newest finished frame
            set_picture(app.next_source, app.next_result);
            app.next_source.clear();
            since = 0; hits = 0;
        }
        int revealed = std::min(hits, strikes);
        float fw = 330, fh = 176, fx = bx + 40, fy = by;
        fill(fx - 2, fy - 2, fw + 4, fh + 4, LIGHT); bevel(fx - 2, fy - 2, fw + 4, fh + 4, true);
        fill(fx, fy, fw, fh, SDL_Color{24, 32, 32, 255});
        float impact_y = fy + 20;
        if (app.picture) {
            float tw, th;
            SDL_GetTextureSize(app.picture, &tw, &th);
            // Quick zoom from the small cut-out into its detail, then only the detail is worked on.
            double zt = std::clamp(since / 0.45, 0.0, 1.0);
            float e = float(1 - (1 - zt) * (1 - zt) * (1 - zt));
            SDL_FRect src{app.focus.x * e, app.focus.y * e, tw + (app.focus.w - tw) * e, th + (app.focus.h - th) * e};
            float s = std::min(fw / src.w, (fh - 16) / src.h);
            SDL_FRect dst{fx + (fw - src.w * s) / 2, fy + (fh - 16 - src.h * s) / 2, src.w * s, src.h * s};
            SDL_RenderTexture(R, app.picture, &src, &dst);
            float done_h = dst.h * revealed / strikes;
            if (app.result && done_h > 0) {
                float rw, rh;
                SDL_GetTextureSize(app.result, &rw, &rh);
                float k = rw / tw;
                SDL_FRect rs{src.x * k, src.y * k, src.w * k, src.h * k * (done_h / dst.h)}, rd{dst.x, dst.y, dst.w, done_h};
                SDL_RenderTexture(R, app.result, &rs, &rd);
            }
            impact_y = dst.y + dst.h * 0.35f;
            float right_edge = dst.x + dst.w;
            app_right_edge = right_edge;
            if (revealed < strikes) line(dst.x, dst.y + done_h, dst.x + dst.w, dst.y + done_h, SDL_Color{0, 255, 0, 255});
            // Chips fly off the right edge just after each strike.
            double after = std::fmod(work, beat) / beat;
            if (hits > 0 && hits <= strikes && after < 0.45) {
                std::mt19937 chips(unsigned(hits * 97));
                for (int k = 0; k < 9; ++k) {
                    float vx = float(chips() % 40) + 10, vy = -float(chips() % 50) - 10;
                    float px = float(dst.x + dst.w - 4 - vx * after * 1.6), py = float(impact_y + vy * after * 2.2 + 120 * after * after);
                    fill(px, py, 3, 3, k % 2 ? SDL_Color{255, 255, 255, 255} : SDL_Color{255, 220, 64, 255});
                }
            }
            text(tiny, dst.x + 4, dst.y + 3, std::to_string(revealed) + " / " + std::to_string(strikes), SDL_Color{0, 255, 0, 255});
        }
        fill(fx, fy + fh - 16, fw, 16, NAVY);
        text(tiny, fx + 6, fy + fh - 14, elide(tiny, app.caption, fw - 12), LIGHT);
        // The mechanic from the player's garage scene; his workpiece is clipped away so he strikes our picture.
        if (smith) {
            size_t frame = size_t(std::fmod(work, beat) / beat * 6) % smith->frames.size();
            float tw, th;
            SDL_GetTextureSize(smith->frames[frame], &tw, &th);
            float x0 = app_right_edge - 46, y0 = impact_y - 54;  // hammer face lands at local (46, 54)
            SDL_FRect d{x0 + smith->offsets[frame].x, y0 + smith->offsets[frame].y, tw, th};
            SDL_Rect top{int(x0), int(y0) - 20, int(smith->w) + 40, 72}, body{int(x0 + 54), int(y0 + 52), int(smith->w), int(smith->h)};
            for (auto& clip : {top, body}) {
                SDL_SetRenderClipRect(R, &clip);
                SDL_RenderTexture(R, smith->frames[frame], nullptr, &d);
            }
            SDL_SetRenderClipRect(R, nullptr);
        }
        float ix = bx + bw - 190;
        text(regular, ix, fy + 10, key == "upscale" ? "Upscaling 3x" : "Remastered", INK);
        text(title, ix, fy + 32, "640 x 480", ACCENT);
        text(title, ix, fy + 56, "> 1920 x 1440", ACCENT);
    } else {  // copy scenes: the hero carries pictures from one folder to the other
        static const std::map<std::string, std::pair<const char*, const char*>> ends{{"extract", {"CD", "PRACOVNÍ SLOŽKA"}},
            {"download", {"INTERNET", "PRACOVNÍ SLOŽKA"}}, {"remaster", {"SNÍMKY", "BALÍČKY"}}, {"app", {"BALÍČKY", "HRA"}}};
        auto e = ends.count(key) ? ends.at(key) : std::pair<const char*, const char*>{"", ""};
        float ground = by + bh - 10, lx = bx + 14, rx = bx + bw - 54;
        fill(bx + 4, ground, bw - 8, 1, SHADOW); fill(bx + 4, ground + 1, bw - 8, 1, LIGHT);
        draw(icon(key == "download" ? "floppy-32" : "folder-32"), lx, ground - 44, 40, 40);
        text(tiny, lx + 20 - text_width(tiny, e.first) / 2, ground - 60, e.first, INK);
        draw(icon(key == "app" ? "gear-32" : "folder-32"), rx, ground - 44, 40, 40);
        text(tiny, rx + 20 - text_width(tiny, e.second) / 2, ground - 60, e.second, INK);
        for (size_t k = 0; k < std::min<size_t>(4, app.recent.size()); ++k)
            text(tiny, rx - 120, ground - 100 + k * 13, elide(tiny, app.recent[app.recent.size() - 1 - k], 110), k ? NOTE : ACCENT);
        Anim* right = anim("DOPRAVA.FLC");
        Anim* left = anim("DOLEVA.FLC");
        if (right && left) {
            const double there = 3.6, back = 2.6;
            double cycle = std::fmod(t, there + back);
            float x0 = lx + 70, x1 = rx - 60;
            if (cycle < there) {
                float x = float(x0 + (x1 - x0) * cycle / there);
                float top = draw_anim(*right, t, x, ground, 0.8f);
                float bob = float(std::sin(t * right->fps / 2 * 3.14159) * 2);
                if (app.picture) shadowed(app.picture, x - 30, top - 44 + bob, 60, 44);
                else draw(icon("document-32"), x - 16, top - 36 + bob, 32, 32);
                // The walk never waits for the build: he carries whatever was unpacked during this trip.
                int done = std::atoi(app.count.c_str());
                if (cycle < 0.05) app.trip_start = done;
                int batch = std::max(1, done - app.trip_start);
                std::string badge = "×" + std::to_string(batch);
                float bw2 = text_width(tiny, badge) + 8;
                fill(x + 24, top - 50 + bob, bw2, 14, NAVY);
                text(tiny, x + 28, top - 49 + bob, badge, LIGHT);
            } else {
                float x = float(x1 - (x1 - x0) * (cycle - there) / back);
                draw_anim(*left, t, x, ground, 0.8f);
            }
        } else {
            const float heights[] = {40, 28, 52};
            for (int k = 0; k < 3; ++k) {
                double phase = std::fmod(t * 0.55 + k / 3.0, 1.0);
                float px = float(lx + 50 + (rx - lx - 60) * phase), py = float(ground - 40 - std::sin(phase * 3.14159) * heights[k]);
                if (app.picture) shadowed(app.picture, px - 24, py - 18, 48, 36); else draw(icon("document-32"), px - 14, py - 14, 28, 28);
            }
        }
    }
}

double overall_fraction() {
    int s = std::max(0, app.stage);
    double total = 0;
    for (int i = 0; i < s; ++i) total += STAGES[i].share;
    return (total + STAGES[s].share * app.fraction) / 100;
}

void remember_session(bool force) {
    if (!app.busy || (!force && SDL_GetTicks() - app.session_saved < 5000)) return;
    app.session_saved = SDL_GetTicks();
    app.session.percent = int(overall_fraction() * 100);
    app.session.stage = std::max(0, app.stage);
    app.session.updated = std::time(nullptr);
    save_session(app.session);
}

// --- CD player: an old hi-fi deck with a monochrome LCD ---------------------------------------------
const SDL_Color LCD_BG{154, 167, 132, 255}, LCD_INK{27, 36, 20, 255}, LCD_GHOST{137, 149, 117, 255};

void seg7(float x, float y, float w, float h, float t, char ch) {
    static const std::map<char, const char*> lit{{'0', "abcdef"}, {'1', "bc"}, {'2', "abged"}, {'3', "abgcd"}, {'4', "fgbc"},
        {'5', "afgcd"}, {'6', "afgedc"}, {'7', "abc"}, {'8', "abcdefg"}, {'9', "abcdfg"}, {'-', "g"}};
    auto it = lit.find(ch);
    std::string on = it == lit.end() ? "" : it->second;
    float g = std::max(1.0f, t * 0.35f), half = h / 2;
    const std::pair<char, SDL_FRect> segs[] = {
        {'a', {x + t, y, w - 2 * t, t}}, {'d', {x + t, y + h - t, w - 2 * t, t}}, {'g', {x + t, y + half - t / 2, w - 2 * t, t}},
        {'f', {x, y + t + g, t, half - 1.5f * t - 2 * g}}, {'e', {x, y + half + t / 2 + g, t, half - 1.5f * t - 2 * g}},
        {'b', {x + w - t, y + t + g, t, half - 1.5f * t - 2 * g}}, {'c', {x + w - t, y + half + t / 2 + g, t, half - 1.5f * t - 2 * g}}};
    for (auto& [k, r] : segs) fill(r.x, r.y, r.w, r.h, on.find(k) != std::string::npos ? LCD_INK : LCD_GHOST);
}

void seg7_text(float x, float y, float w, float h, float t, const std::string& str) {
    for (char c : str) {
        if (c == ':') { fill(x, y + h * 0.28f, t, t, LCD_INK); fill(x, y + h * 0.64f, t, t, LCD_INK); x += t + 3; continue; }
        seg7(x, y, w, h, t, c);
        x += w + 3;
    }
}

void wedge(float cx, float cy, float r0, float r1, float a0, float a1, SDL_Color c) {
    SDL_FColor fc{c.r / 255.f, c.g / 255.f, c.b / 255.f, 1};
    SDL_Vertex v[4];
    float as[2] = {a0, a1};
    for (int i = 0; i < 2; ++i) {
        v[i * 2] = {{cx + r0 * std::cos(as[i]), cy + r0 * std::sin(as[i])}, fc, {0, 0}};
        v[i * 2 + 1] = {{cx + r1 * std::cos(as[i]), cy + r1 * std::sin(as[i])}, fc, {0, 0}};
    }
    const int idx[6] = {0, 1, 3, 0, 3, 2};
    SDL_RenderGeometry(R, nullptr, v, 4, idx, 6);
}

void ring(float cx, float cy, float r, float th, SDL_Color c) {
    for (int i = 0; i < 48; ++i) wedge(cx, cy, r - th, r, i * 6.2832f / 48, (i + 1) * 6.2832f / 48 + 0.01f, c);
}

void cd_player(float x, float y, float w, float h) {
    auto& p = app.player;
    if (SDL_GetTicks() - app.player_checked > 2000) { app.player_checked = SDL_GetTicks(); p.set_folder(app.work / "data" / "audio"); }
    fill(x, y, w, h, FACE);
    bevel(x, y, w, h, false);
    float lx = x + 6, ly = y + 6, lw = 320, lh = h - 12;
    fill(lx, ly, lw, lh, LCD_BG);
    bevel(lx, ly, lw, lh, true);
    // Spinning LCD disc: rim, hub and eight segments, three of them dark.
    float r = lh * 0.4f, ccx = lx + 8 + r, ccy = ly + lh / 2;
    if (p.playing()) app.cd_angle += 0.09;
    ring(ccx, ccy, r, 2, LCD_INK);
    ring(ccx, ccy, r * 0.24f, 2, LCD_INK);
    for (int i = 0; i < 8; ++i) {
        float a = float(i * 0.7854 + app.cd_angle);
        wedge(ccx, ccy, r * 0.38f, r * 0.82f, a, a + 0.5f, i % 3 == 0 ? LCD_INK : LCD_GHOST);
    }
    float tx = ccx + r + 12, ty = ly + 6;
    bool disc = p.has_disc();
    bool showing_volume = SDL_GetTicks() < app.volume_shown;
    if (!disc) {
        text(bold, tx, ty + 6, "NO DISC", LCD_INK);
    } else if (showing_volume) {
        text(bold, tx, ty, "VOL", LCD_INK);
        char v[8];
        std::snprintf(v, sizeof v, "%02d", p.volume());
        seg7_text(tx + 34, ty, 13, 24, 3, v);
        for (int i = 0; i < 20; ++i) fill(tx + i * 6, ty + 32, 4, 5, i < p.volume() ? LCD_INK : LCD_GHOST);
    } else {
        text(tiny, tx, ty, p.playing() ? "\u25BA PLAY" : "\u25A0 STOP", LCD_INK);
        char t[8];
        int sec = int(p.seconds());
        if (p.playing()) std::snprintf(t, sizeof t, "%02d:%02d", sec / 60, sec % 60);
        else std::snprintf(t, sizeof t, "--:--");
        seg7_text(tx, ty + 12, 13, 24, 3, t);
        text(tiny, tx + 90, ty, "TRACK", LCD_INK);
        char n[4];
        std::snprintf(n, sizeof n, "%02d", p.track());
        seg7_text(tx + 90, ty + 12, 13, 24, 3, n);
    }
    // Five-band analyzer from short horizontal dashes, with a held peak above each band.
    if (disc && !showing_volume) {
        auto lv = p.levels();
        float sx = lx + lw - 92, sy = ly + 5;
        const int rows = 8;
        for (int b = 0; b < 5; ++b) {
            int on = int(lv[b] * rows + 0.5f);
            if (lv[b] >= app.peaks[b] || SDL_GetTicks() - app.peak_at[b] > 700) { app.peaks[b] = lv[b]; app.peak_at[b] = SDL_GetTicks(); }
            int peak = int(app.peaks[b] * rows + 0.5f);
            for (int row = 0; row < rows; ++row) {
                bool lit = row < on || (row == peak && peak > 0 && p.playing());
                fill(sx + b * 18, sy + (rows - 1 - row) * 5, 14, 3, lit && p.playing() ? LCD_INK : LCD_GHOST);
            }
        }
        text(tiny, sx - 2, sy + rows * 5 + 1, "60 250 1k 4k 12k", LCD_INK);
    }
    // Controls.
    float bx = lx + lw + 10;
    text(tiny, bx, y + 10, "HUDBA Z HERNÍCH DAT", INK);
    float by = y + 28;
    static bool autoplay = SDL_getenv("HB_TEST_PLAY") != nullptr;  // dev hook (with SDL_AUDIO_DRIVER=dummy)
    if (button(bx, by, 30, 24, "\u25BA", disc) || (autoplay && disc)) { p.play(); autoplay = false; }
    if (button(bx + 34, by, 30, 24, "\u25A0", disc)) p.stop();
    if (button(bx + 68, by, 30, 24, "\u2212", disc)) { p.volume(-1); app.volume_shown = SDL_GetTicks() + 2000; }
    if (button(bx + 102, by, 30, 24, "+", disc)) { p.volume(+1); app.volume_shown = SDL_GetTicks() + 2000; }
    static bool show_volume = SDL_getenv("HB_TEST_VOLUME") != nullptr;
    if (show_volume && disc) { p.volume(+1); app.volume_shown = SDL_GetTicks() + 2000; show_volume = false; }
}

void page_build() {
    int s = std::max(0, app.stage);
    float x = 22, w = W - 44, y = CY + 12;
    char head[64];
    std::snprintf(head, sizeof head, "KROK %d ZE 7 / %s", s + 1, EYEBROW[s]);
    eyebrow(x, y, head);
    text(heading, x, y + 16, app.failed ? "Tohle se nepovedlo" : STAGES[s].title);
    text(regular, x, y + 48, elide(regular, app.failed ? app.error : STAGES[s].why, w - 140), app.failed ? FAILC : NOTE);
    double total = overall_fraction();
    std::string pct = std::to_string(int(total * 100));
    text(huge, x + w - 50 - text_width(huge, pct), y + 14, pct, ACCENT);
    text(title, x + w - 24, y + 28, "%", ACCENT);
    stage_view(x, y + 72, w, 222);
    float sy = y + 304;
    text(bold, x, sy, app.count.empty() ? "-" : app.count, ACCENT);
    std::string pace = app.speed.empty() ? "" : app.speed + (app.eta.empty() ? "" : " / zbývá asi " + app.eta);
    text(small, x + w - text_width(small, pace), sy + 2, pace, INK);
    text(small, x, sy + 22, "Celkem hotovo", INK);
    text(small, x + w - text_width(small, pct + " %"), sy + 22, pct + " %", INK);
    segments(x, sy + 38, w, 20, total);
    text(mono, x, sy + 66, elide(mono, "Právě: " + (app.caption.empty() ? std::string("-") : app.caption), w / 2), INK);
    if (app.busy) {
        int minutes = int(std::chrono::duration<double>(std::chrono::steady_clock::now() - app.started).count() / 60);
        std::string run = "Běží " + std::to_string(minutes) + " min";
        text(mono, x + w - text_width(mono, run), sy + 66, run, INK);
    }
    fill(x, sy + 86, w, 1, SHADOW);
    if (STAGES[s].key == std::string("extract") || STAGES[s].key == std::string("app")) text(tiny, x, sy + 94, "KOPÍROVÁNÍ HERNÍCH DAT", ACCENT);
    cd_player(W - 22 - 470, sy + 112, 470, 72);
    if (app.failed) {
        if (button(20, H - 44, 72, 32, "Zpět")) app.page = 0;
        if (button(W - 170, H - 44, 150, 32, "Zkusit znovu", true, true)) start_checks();
    } else {
        text(regular, 20, H - 34, "Při přerušení opětovné spuštění naváže na předchozí práci.", NOTE);
    }
}

void page_done() {
    hatch(24, CY + 30, 300, 420);
    if (app.character) {
        float tw, th;
        SDL_GetTextureSize(app.character, &tw, &th);
        float s = std::floor(std::min(260 / tw, 380 / th));
        draw(app.character, 24 + (300 - tw * s) / 2, CY + 40 + (400 - th * s) / 2, tw * s, th * s);
    }
    fill(270, CY + 400, 40, 40, FACE); bevel(270, CY + 400, 40, 40); draw(icon("check-32"), 274, CY + 404, 32, 32);
    float x = 360, w = W - x - 24, y = CY + 60;
    eyebrow(x, y, "SESTAVENO / 100 %");
    text(heading, x, y + 16, "Hotovo. Můžeš hrát.");
    text(title, x, y + 50, "Teď už to můžeš pokazit jen hraním.", INK);
    const char* facts[] = {"595 položek / 4 903 snímků", "640 x 480 > 1920 x 1440", "Hudební stopy 2 a 3"};
    for (int i = 0; i < 3; ++i) { draw(icon("check-16"), x, y + 86 + i * 24, 16, 16); text(small, x + 24, y + 88 + i * 24, facts[i], INK); }
    float py = y + 166;
    fill(x, py, w, 56, SDL_Color{200, 200, 200, 255}); bevel(x, py, w, 56, true);
    text(mono, x + 10, py + 10, elide(mono, app.result_path.parent_path().string(), w - 20), NOTE);
    text(mono, x + 10, py + 30, app.result_path.filename().string(), INK);
    if (button(x, py + 74, 150, 34, "Spustit hru", true, true) || in.enter) launch_game();
    draw(icon("play-16"), x + 14, py + 83, 16, 16);
#ifdef _WIN32
    if (button(x + 162, py + 78, 190, 28, "Ukázat v Průzkumníku")) reveal();
#else
    if (button(x + 162, py + 78, 170, 28, "Ukázat ve Finderu")) reveal();
#endif
    text(small, x, py + 124, "F8 / G: Original / Remastered. Podržením mezerníku ukážeš aktivní místa.", NOTE);
    if (app.cleanup) text(small, x, py + 142, "Dočasné soubory odstraněny.", NOTE);
    if (button(W - 120, H - 44, 100, 32, "Zavřít")) { SDL_Event e{}; e.type = SDL_EVENT_QUIT; SDL_PushEvent(&e); }
}

void pump_messages() {
    std::deque<std::string> lines;
    std::deque<std::pair<int, std::string>> paths;
    { std::lock_guard g(app.lock); lines.swap(app.messages); paths.swap(app.pending_paths); }
    for (auto& [which, path] : paths) {
        if (which == 0) {
            app.image = path; app.use_drive = false;
            try { app.detected = "Rozpoznáno: " + format_name(detect_format(fs::u8path(path))); } catch (const std::exception& e) { app.detected = e.what(); }
        } else if (which == 1) { app.output = path; if (app.page == 1) start_checks(); }
        else { app.archive = path; start_checks(); }
    }
    for (auto& line : lines) {
        if (line.rfind("[upd-found]", 0) == 0) {
            app.update = App::Update::Found; app.update_at = SDL_GetTicks();
            if (SDL_getenv("HB_TEST_UPDATE_GO")) start_update_download();  // dev hook
            continue;
        }
        if (line.rfind("[upd] Download ", 0) == 0) { app.update_fraction = std::atoi(line.c_str() + 15) / 100.0; continue; }
        if (line.rfind("[upd-done] ", 0) == 0) { app.update_file = fs::u8path(line.substr(11)); app.update = App::Update::Starting; app.update_at = SDL_GetTicks(); continue; }
        if (line.rfind("[upd-error] ", 0) == 0) { app.update_error = line.substr(12); app.update = App::Update::Failed; cancelled = false; continue; }
        if (line.rfind("[game-outdated] ", 0) == 0) {
            if (auto info = inspect_game(fs::u8path(line.substr(16)))) app.outdated.push_back(*info);
            if (app.game == App::GameUpdate::None && !app.outdated.empty()) { app.game_info = app.outdated.front(); app.outdated.pop_front(); app.game = App::GameUpdate::Offer; app.game_at = SDL_GetTicks(); }
            continue;
        }
        if (line.rfind("[game] ", 0) == 0) { app.game_status = line.substr(7); add_log(line.substr(7)); continue; }
        if (line.rfind("[game-done]", 0) == 0) { app.game = App::GameUpdate::Done; app.game_at = SDL_GetTicks(); remember_game(app.game_info.app); continue; }
        if (line.rfind("[game-error] ", 0) == 0) { app.game = App::GameUpdate::Failed; app.game_error = line.substr(13); add_log("CHYBA: " + app.game_error); continue; }
        if (line.rfind("[result] ", 0) == 0) { remember_game(fs::u8path(line.substr(9))); clear_session(); app.busy = false; app.result_path = fs::u8path(line.substr(9)); if (!app.character) set_stage(6); app.page = 3; }
        else if (line.rfind("[error] ", 0) == 0) { remember_session(true); app.busy = false; app.failed = true; app.error = line.substr(8); add_log("CHYBA: " + app.error); app.details = true; }
        else handle(line);
    }
}
}

int run_window(int, char**) {
    SDL_SetHint(SDL_HINT_APP_NAME, "Horké léto Remastered");
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    app.window = SDL_CreateWindow("Horké léto Remastered - sestavení", W, H, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    R = SDL_CreateRenderer(app.window, nullptr);
    SDL_SetRenderVSync(R, 1);
    density = SDL_GetWindowPixelDensity(app.window);
    SDL_SetRenderScale(R, density, density);
    regular.load("DejaVuSans.ttf", 12, false);
    bold.load("DejaVuSans-Bold.ttf", 12, false);
    title.load("DejaVuSans.ttf", 16, false);
    big.load("DejaVuSans-Bold.ttf", 18, false);
    mono.load("DejaVuSansMono.ttf", 12, false);
    tiny.load("DejaVuSansMono.ttf", 10, false);
    small.load("DejaVuSans.ttf", 11, false);
    heading.load("DejaVuSans.ttf", 24, false);
    huge.load("DejaVuSans.ttf", 32, false);
    if (auto* data = find_embedded("builder-icon.png")) {
        auto image = convert(load_png_memory(Bytes(data->data, data->data + data->size), 4), 4);
        image = resize_nearest(image, 256, 256);
        if (auto* surface = SDL_CreateSurfaceFrom(image.w, image.h, SDL_PIXELFORMAT_ABGR8888, image.px.data(), image.w * 4)) {
            SDL_SetWindowIcon(app.window, surface);
            SDL_DestroySurface(surface);
        }
    }
    app.drives = optical_drives();
    if (!SDL_getenv("HB_DEMO")) app.resume = load_session();
    app.replaced = replaced_version;
    if (!app.replaced.empty()) app.update_at = SDL_GetTicks();
    start_update_check();
    find_outdated_games();
    if (SDL_getenv("HB_DEMO_DETAILS")) {  // dev hook: open the log drawer with sample lines
        app.details = true;
        for (auto line : {"Ověřujeme tvoji zálohu", "[detail] Rozpoznaný formát: BIN/CUE, 179181 sektorů", "[detail] HL.EXE sedí, datová stopa odpovídá podporovanému vydání",
                          "Vybalujeme a ověřujeme obrázky FLC", "[detail] A1-KAME!.FLC: 1 snímků, nově 1, z mezipaměti 0, nových zvětšení 2, atlasy 2 stran (0.1 MB), 1.8 s"})
            add_log(line);
    }
    if (const char* test = SDL_getenv("HB_TEST_GAME_DIALOG")) {  // dev hook: "offer" or "running" over a fake game
        app.game_info = GameInfo{fs::u8path("/Users/hrac/Hry/Horke Leto/Horke Leto.app"), "", true};
        app.game_info.outdated_engine = std::string(test) != "offer-images";app.game_info.variants_stale = !app.game_info.outdated_engine;
        std::string state = test;
        app.game = state == "running" ? App::GameUpdate::Running : state == "done" ? App::GameUpdate::Done
                 : state == "failed" ? App::GameUpdate::Failed : App::GameUpdate::Offer;
        if (state == "failed") app.game_error = "Podepsání hry selhalo.";
        if (state == "offer-variants") app.game_info.missing_variants = 159;
        app.game_status = "Ponechávám herní data";
        app.game_at = SDL_GetTicks();
    }
    if (const char* home = SDL_GetUserFolder(SDL_FOLDER_HOME)) app.output = (fs::u8path(home) / "Games" / "Horke Leto").string();
    // Developer/README screenshots: HB_DEMO="page stage" with HB_DEMO_WORK pointing at a .builder folder.
    if (const char* demo = SDL_getenv("HB_DEMO")) {
        int page = 0, stage = 0;
        std::sscanf(demo, "%d %d", &page, &stage);
        app.image = SDL_getenv("HB_DEMO_IMAGE") ? SDL_getenv("HB_DEMO_IMAGE") : "/Users/hrac/Hry/HorkeLeto.cue";
        app.detected = "Rozpoznáno: BIN/CUE";
        app.output = "/Users/hrac/Games/Horke Leto";  // never show the developer's real home in screenshots
        if (const char* work = SDL_getenv("HB_DEMO_WORK")) app.work = fs::u8path(work);
        if (page == 1) start_checks();
        if (page == 2) {
            app.page = 2; app.busy = true; app.started = std::chrono::steady_clock::now() - std::chrono::minutes(31);
            set_stage(stage);
            const char* names[] = {"", "A1-KAME!.FLC", "", "", "B2-PZ14.FLC", "C1-VSTAV.FLC", ""};
            if (stage == 1) set_picture(app.work / "frames" / names[1] / "frame-0.png");
            if (stage == 3) {
                int n = 0;
                for (auto& e : fs::directory_iterator(app.work / "art-decrypted" / "images")) {
                    if (n++ % 37) continue;
                    app.thumbs.push_back(thumbnail(e.path()));
                    app.caption = e.path().stem().string();
                    if (app.thumbs.size() == 12) break;
                }
                app.count = "126 / 316";
            }
            if (stage == 4 || stage == 5) {
                auto pack = stage == 4 ? "original" : "remastered";
                set_picture(app.work / "frames" / names[stage] / "frame-0.png", app.work / "packs" / pack / "flc" / names[stage] / "overlay-0.png");
            }
            progress(stage == 4 ? 312.0 / 595 : 0.47, stage == 4 ? "312 / 595" : "", names[stage]);
            app.speed = "9.5 obr./min"; app.eta = "30 min";
            app.recent = {"A1-KAME!.FLC", "A1-KAME@.FLC", "A1-LAVI!.FLC", "A1-LEZE!.FLC"};
            app.picture_at = SDL_GetTicks() + 300;
            if (SDL_getenv("HB_DEMO_FAILED")) { app.failed = true; app.busy = false; app.error = "Herní soubor chybí nebo nesedí: GFX.GRP"; }
        }
        if (page == 3) {
            app.page = 3; app.result_path = "/Users/hrac/Hry/Horke Leto/Horke Leto.app";
            auto frame = app.work / "frames" / "C1-VSTAV.FLC" / "frame-15.png";
            if (fs::exists(frame)) app.character = texture(load_png(frame, 4));
        }
    }
    Uint64 opened = SDL_GetTicks();
    bool running = true;
    while (running) {
        in.pressed = in.released = in.enter = false;
        in.wheel = 0;
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 33)) {
            do {
                switch (e.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if (app.busy) {
                        const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Přerušit"}, {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Pokračovat"}};
                        SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING, app.window, "Sestavení běží",
                            "Opravdu přerušit? Hotová část zůstane uložená a příště naváže.", 2, buttons, nullptr};
                        int choice = 0;
                        SDL_ShowMessageBox(&box, &choice);
                        if (choice == 1) { remember_session(true); cancelled = true; taskbar_progress(app.window, TaskbarState::None, 0); std::_Exit(0); }
                    } else running = false;
                    break;
                case SDL_EVENT_MOUSE_MOTION: in.x = e.motion.x; in.y = e.motion.y; break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN: if (e.button.button == SDL_BUTTON_LEFT) { in.down = in.pressed = true; in.x = e.button.x; in.y = e.button.y; } break;
                case SDL_EVENT_MOUSE_BUTTON_UP: if (e.button.button == SDL_BUTTON_LEFT) { in.down = false; in.released = true; in.x = e.button.x; in.y = e.button.y; } break;
                case SDL_EVENT_MOUSE_WHEEL: in.wheel += e.wheel.y; break;
                case SDL_EVENT_KEY_DOWN: if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) in.enter = true; break;
                }
            } while (SDL_PollEvent(&e));
        }
        pump_messages();
        remember_session();
        if (app.busy) taskbar_progress(app.window, TaskbarState::Normal, overall_fraction());
        else if (app.failed) taskbar_progress(app.window, TaskbarState::Error, 1);
        else taskbar_progress(app.window, TaskbarState::None, 0);
        if (app.page == 3 && !app.attention) { app.attention = true; taskbar_attention(app.window); }
        watch_extract();
        fill(0, 0, W, H, FACE);
        chrome();
        bool updating = app.update == App::Update::Downloading || app.update == App::Update::Starting || app.update == App::Update::Failed;
        bool resuming = app.page == 0 && app.resume && !app.details;
        bool game_dialog = !resuming && !app.busy && app.game != App::GameUpdate::None && !app.details;
        Input real = in;
        if (updating || resuming || game_dialog) { in.pressed = in.released = in.enter = false; in.wheel = 0; in.x = in.y = -1000; }  // the page behind is inert
        if (app.details) log_drawer();
        else switch (app.page) {
        case 0: page_source(); break;
        case 1: page_checks(); break;
        case 2: page_build(); break;
        default: page_done(); break;
        }
        in = real;
        if (updating) update_dialog();
        else if (game_dialog) game_update_dialog();
        else {
            if (resuming) resume_dialog();
            if (app.page == 0 || app.page == 3) update_toast();
        }
        if (app.quit) running = false;
        if (app.page != 2 && app.player.playing()) app.player.shutdown();  // the player lives only on the build screen
        const char* shot = SDL_getenv("HB_SCREENSHOT");
        const char* wait = SDL_getenv("HB_SCREENSHOT_MS");
        bool settled = !app.checking && SDL_GetTicks() - opened > Uint64(wait ? std::atoi(wait) : 1500);
        if (shot && settled) {  // developer: save a frame and quit (read before present)
            if (auto* surface = SDL_RenderReadPixels(R, nullptr)) { SDL_SaveBMP(surface, shot); SDL_DestroySurface(surface); }
            running = false;
        }
        SDL_RenderPresent(R);
    }
    if (app.worker.joinable()) app.worker.detach();
    app.player.shutdown();
    if (app.update_worker.joinable()) app.update_worker.detach();
    if (app.game_worker.joinable()) app.game_worker.join();  // never leave a half-swapped game behind
    SDL_Quit();
    return 0;
}
