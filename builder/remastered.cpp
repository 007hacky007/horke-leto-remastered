// The Remastered pack: decrypted redraws for statics, colour-graded Original upscales for animations.
#include "remastered.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

namespace hb {
namespace {
struct Context {
    const RemasteredPaths& p;
    const Upscaler& u;
    json manifest;
    std::array<double, 3> gains{};
    std::string base_key;
};

Image grade(Image image, const std::array<double, 3>& gains) {
    image = convert(image, 4);
    for (size_t i = 0, n = size_t(image.w) * image.h; i < n; ++i) {
        uint8_t* px = &image.px[i * 4];
        if (px[3] == 0) { px[0] = px[1] = px[2] = 0; continue; }
        for (int k = 0; k < 3; ++k) px[k] = uint8_t(std::clamp(std::nearbyint(px[k] * gains[k]), 0.0, 255.0));
    }
    return image;
}

// The exported source frame on a 640x480 RGBA canvas (index 0 transparent).
Image source_canvas(const Context& c, const std::string& name, int index) {
    auto meta = read_json(c.p.source / (name + ".json")).at("frames").at(index);
    Image canvas(640, 480, 4);
    if (meta.at("crop").at("empty").get<bool>()) return canvas;
    auto indices = load_png(c.p.source / name / meta.at(".indices.png").at("path").get<std::string>(), 1);
    Image rgba(indices.w, indices.h, 4);
    auto& palette = meta.at("palette");
    for (size_t i = 0; i < indices.px.size(); ++i) {
        uint8_t v = indices.px[i];
        if (!v) continue;
        for (int k = 0; k < 3; ++k) rgba.px[i * 4 + k] = palette[v][k].get<uint8_t>();
        rgba.px[i * 4 + 3] = 255;
    }
    paste(canvas, rgba, meta.at("offset")[0], meta.at("offset")[1]);
    return canvas;
}

// Where an animation frame shows the unchanged background, use the redrawn background instead.
Image substitute(const Image& src, const Image& bg, Image selected, const Image& redraw_in) {
    auto redraw = convert(redraw_in, 4);
    std::vector<uint8_t> same(640 * 480), painted(640 * 480);
    bool any = false;
    for (size_t i = 0; i < same.size(); ++i) {
        painted[i] = src.px[i * 4 + 3] > 0;
        same[i] = painted[i] && !std::memcmp(&src.px[i * 4], &bg.px[i * 4], 3);
        any |= same[i] != 0;
    }
    std::vector<uint8_t> high(same);
    if (any) {
        auto nearest = nearest_set(painted, 640, 480);
        for (size_t i = 0; i < same.size(); ++i) if (!painted[i] && same[size_t(nearest[i])]) high[i] = 2;  // spill
    }
    for (int y = 0; y < selected.h; ++y)
        for (int x = 0; x < selected.w; ++x) {
            uint8_t v = high[size_t(y * 480 / selected.h) * 640 + x * 640 / selected.w];
            uint8_t* d = selected.at(x, y);
            if (v == 1 || (v == 2 && d[3] > 0)) std::memcpy(d, redraw.at(x, y), 4);
        }
    return selected;
}

std::pair<Image, json> overlay_tile(const Image& selected) {
    std::array<int, 4> box;
    if (!bbox(selected, box)) return {Image(3, 3, 4), {{"x", 0}, {"y", 0}, {"width", 0}, {"height", 0}, {"empty", true}}};
    int x = box[0] / 3, y = box[1] / 3, right = (box[2] + 2) / 3, bottom = (box[3] + 2) / 3;
    auto tile = crop(selected, x * 3, y * 3, (right - x) * 3, (bottom - y) * 3);
    for (size_t i = 0; i < tile.px.size(); i += 4) if (!tile.px[i + 3]) std::memset(&tile.px[i], 0, 4);
    return {tile, {{"x", x}, {"y", y}, {"width", right - x}, {"height", bottom - y}, {"empty", false}}};
}

struct AtlasCache { json manifest; std::string atlas_name; Image atlas; };

Image original_canvas(const Context& c, const std::string& name, int index, AtlasCache& cache) {
    auto folder = c.p.original / "flc" / name;
    if (cache.manifest.is_null()) cache.manifest = read_json(folder / "index.json");
    auto& usage = cache.manifest.at("frames").at(index).at("usages").at("overlay");
    std::string key = usage.at("atlas");
    if (cache.atlas_name != key) { cache.atlas = load_png(folder / key, 4); cache.atlas_name = key; }
    Image out(1920, 1440, 4);
    if (!usage.at("empty").get<bool>()) {
        auto& r = usage.at("rect");
        paste(out, crop(cache.atlas, r[0], r[1], r[2], r[3]), usage.at("logical_offset")[0].get<int>() * 3, usage.at("logical_offset")[1].get<int>() * 3);
    }
    return out;
}

Image legacy(const Context& c, const std::string& name, const fs::path& folder) {
    auto source = source_canvas(c, name, 0);
    auto path = folder / "legacy.png";
    std::string key = sha_bytes(source.px) + c.u.runner_sha256;
    Checkpoint checkpoint(folder / "legacy-complete.json", key);
    if (!checkpoint.valid()) {
        Image rgb(640, 480, 3);
        std::vector<uint8_t> mask(640 * 480);
        for (size_t i = 0; i < mask.size(); ++i) { std::memcpy(&rgb.px[i * 3], &source.px[i * 4], 3); mask[i] = source.px[i * 4 + 3] >= 128; }
        auto in = folder / "legacy-in", out = folder / "legacy-out";
        remove_tree(in); remove_tree(out);
        save_png(extend_rgb(rgb, mask), in / "legacy.png");
        fs::create_directories(out);
        run_upscaler(c.u, in, out, folder / "legacy.log", "1:1:1");
        fs::rename(out / "legacy.png", path);
        remove_tree(in); remove_tree(out);
        checkpoint.commit({path});
    }
    auto image = load_png(path, 4);
    auto alpha = resize_nearest(source, image.w, image.h);
    for (size_t i = 0; i < image.px.size(); i += 4) image.px[i + 3] = alpha.px[i + 3];
    return grade(image, c.gains);
}

Image selected(const Context& c, const std::string& name, int index, const fs::path& folder, AtlasCache& cache) {
    auto& statics = c.manifest.at("statics");
    if (statics.contains(name) && statics.at(name).contains("file")) {
        auto& row = statics.at(name);
        auto path = c.p.art / row.at("file").get<std::string>();
        if (sha_file(path) != row.at("sha256").get<std::string>()) throw UserError("Překresba nesedí s kontrolním součtem: " + name);
        return load_png(path, 4);
    }
    if (statics.contains(name) && statics.at(name).value("recipe", "") == "legacy-full-canvas") return legacy(c, name, folder);
    auto output = grade(original_canvas(c, name, index, cache), c.gains);
    auto& animations = c.manifest.at("animations");
    if (animations.contains(name)) {
        auto& background = animations.at(name).at("background");
        if (background.is_string() && !background.get<std::string>().empty()) {
            std::string bg = background;
            auto redraw = load_png(c.p.art / statics.at(bg).at("file").get<std::string>(), 4);
            output = substitute(source_canvas(c, name, index), source_canvas(c, bg, 0), output, redraw);
        }
    }
    return output;
}

Image selected_cached(const Context& c, const std::string& name, int index, AtlasCache& cache, const std::string& key) {
    auto folder = c.p.cache / "remastered-selected" / name;
    fs::create_directories(folder);
    auto path = folder / (key + ".png");
    Checkpoint checkpoint(folder / (key + ".json"), key);
    if (checkpoint.valid()) return load_png(path, 4);
    auto image = selected(c, name, index, folder, cache);
    save_png(image, path);
    checkpoint.commit({path});
    return image;
}

void build_resource(const Context& c, const std::string& name) {
    auto original_index = c.p.original / "flc" / name / "index.json";
    auto folder = c.p.output / "flc" / name;
    fs::create_directories(folder);
    std::string key = sha_file(original_index) + c.base_key;
    Checkpoint checkpoint(folder / ".complete.json", key);
    if (checkpoint.valid()) { c.p.log("[detail] " + name + ": hotovo dříve, ověřeno"); return; }
    auto began = std::chrono::steady_clock::now();
    auto result = read_json(original_index);
    result["mode"] = "Remastered";
    result["atlases"] = json::array();
    result["config_sha256"] = sha_text(key);
    result["art_manifest_sha256"] = sha_file(c.p.art / "manifest.json");
    result["parameters"] = {{"scale", 3}, {"atlas_gutter", 2}, {"grade_rgb_gain", c.manifest.at("grade_rgb_gain")},
        {"overlay", "Selected RGBA, hidden RGB zero, alpha bbox rounded out to logical pixels"},
        {"opaque_base", "Selected RGBA over source palette index zero; graded for animations"},
        {"animation", "Graded Original overlay with exact source-background RGB substitution"}};
    result.erase("model");
    AtlasCache cache;
    std::vector<std::string> keys;
    for (auto& frame : result.at("frames")) {
        auto& usage = frame.at("usages").at("overlay");
        json identity = {frame.at("indices_sha256"), frame.at("palette_sha256"), usage.at("pixel_sha256"), usage.at("logical_offset"), usage.at("empty")};
        keys.push_back(sha_text(key + identity.dump()));
    }
    std::vector<std::array<int, 2>> sizes;
    std::vector<json> crops;
    for (auto& frame : result.at("frames")) {
        int index = frame.at("frame");
        auto image = selected_cached(c, name, index, cache, keys[index]);
        if (image.w != 1920 || image.h != 1440) throw UserError("Překresba má špatnou velikost: " + name);
        auto [tile, crop] = overlay_tile(image);
        sizes.push_back({tile.w, tile.h});
        crops.push_back(crop);
    }
    bool animation = c.manifest.at("animations").contains(name);
    for (std::string usage : {"overlay", "opaque_base"}) {
        auto [pages, places] = atlas_layout(usage == "overlay" ? sizes : std::vector<std::array<int, 2>>(sizes.size(), {1920, 1440}));
        for (size_t page = 0; page < pages.size(); ++page) {
            Image atlas(pages[page][0], pages[page][1], 4);
            for (size_t i = 0; i < places.size(); ++i) {
                auto [pg, x, y, w, h] = places[i];
                if (size_t(pg) != page) continue;
                auto& frame = result.at("frames")[i];
                auto image = selected_cached(c, name, frame.at("frame"), cache, keys[i]);
                Image tile;
                if (usage == "overlay") tile = overlay_tile(image).first;
                else {
                    std::array<double, 3> zero{frame.at("palette")[0][0].get<double>(), frame.at("palette")[0][1].get<double>(), frame.at("palette")[0][2].get<double>()};
                    if (animation) for (int k = 0; k < 3; ++k) zero[k] = std::clamp(std::nearbyint(zero[k] * c.gains[k]), 0.0, 255.0);
                    tile = Image(image.w, image.h, 4);
                    for (size_t k = 0; k < tile.px.size(); k += 4) { tile.px[k] = uint8_t(zero[0]); tile.px[k + 1] = uint8_t(zero[1]); tile.px[k + 2] = uint8_t(zero[2]); tile.px[k + 3] = 255; }
                    alpha_composite(tile, image);
                }
                paste(atlas, extruded(tile), x - 2, y - 2);
                json crop = usage == "overlay" ? crops[i] : json{{"x", 0}, {"y", 0}, {"width", 640}, {"height", 480}, {"empty", false}};
                frame["usages"][usage] = {{"atlas", usage + "-" + std::to_string(page) + ".png"}, {"rect", {x, y, w, h}}, {"crop", crop},
                    {"logical_offset", {crop.at("x"), crop.at("y")}}, {"empty", crop.at("empty")}, {"pixel_sha256", sha_bytes(tile.px)}};
            }
            auto path = folder / (usage + "-" + std::to_string(page) + ".png");
            save_png(atlas, path);
            result["atlases"].push_back({{"path", path.filename().string()}, {"file", path.string()}, {"sha256", sha_file(path)},
                {"bytes", fs::file_size(path)}, {"size", {pages[page][0], pages[page][1]}}, {"usage", usage}});
        }
    }
    write_json(folder / "index.json", result);
    {
        auto statics = c.manifest.at("statics");
        bool redraw = statics.contains(name) && statics.at(name).contains("file");
        std::string method = redraw ? "překresba " + statics.at(name).at("file").get<std::string>()
                           : animation ? "animace, barevná korekce" + std::string(c.manifest.at("animations").at(name).contains("background") ? " + vyměněné pozadí" : "")
                                       : "zvětšený originál";
        uint64_t bytes = 0;
        for (auto& a : result.at("atlases")) bytes += a.value("bytes", uint64_t(0));
        char line[400];
        std::snprintf(line, sizeof line, "[detail] %s: %s, %zu snímků, atlasy %zu stran (%.1f MB), %.1f s", name.c_str(), method.c_str(),
                      result.at("frames").size(), result.at("atlases").size(), bytes / 1048576.0,
                      std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count());
        c.p.log(line);
    }
    std::vector<fs::path> files{folder / "index.json"};
    for (auto& a : result.at("atlases")) files.push_back(folder / a.at("path").get<std::string>());
    checkpoint.commit(files);
}
}

void build_remastered(const RemasteredPaths& paths, const Upscaler& upscaler,
                      const std::function<void(int, int, const std::string&)>& progress) {
    Context c{paths, upscaler, read_json(paths.art / "manifest.json")};
    auto& gain = c.manifest.at("grade_rgb_gain");
    c.gains = {gain[0].get<double>(), gain[1].get<double>(), gain[2].get<double>()};
    c.base_key = sha_file(paths.art / "manifest.json") + "hb-remastered-1";
    std::vector<std::string> names;
    for (auto& [k, v] : c.manifest.at("statics").items()) names.push_back(k);
    for (auto& [k, v] : c.manifest.at("animations").items()) names.push_back(k);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    names.erase(std::remove_if(names.begin(), names.end(), [](const std::string& n) { return !selected_resource(n); }), names.end());
    std::atomic<size_t> next{0};
    std::atomic<int> done{0};
    std::mutex lock;
    std::exception_ptr failure;
    std::vector<std::thread> workers;
    for (int w = 0; w < 4; ++w)
        workers.emplace_back([&] {
            for (size_t i; (i = next++) < names.size();) {
                try {
                    check_cancelled();
                    build_resource(c, names[i]);
                    std::lock_guard g(lock);
                    progress(++done, int(names.size()), names[i]);
                } catch (...) { std::lock_guard g(lock); if (!failure) failure = std::current_exception(); next = names.size(); }
            }
        });
    for (auto& t : workers) t.join();
    if (failure) std::rethrow_exception(failure);
    if (!selected_resource("font")) return;
    fs::create_directories(paths.output / "font");
    for (auto name : {"glyphs.png", "index.json"})
        fs::copy_file(paths.original / "font" / name, paths.output / "font" / name, fs::copy_options::overwrite_existing);
}
}

namespace hb {
// True when the palette change leaves the Original upscale visually the same (only unused colours differ).
static bool palette_invisible(const fs::path& base, const fs::path& variant) {
    auto a = read_json(base / "index.json"), b = read_json(variant / "index.json");
    if (a.at("frames").size() != b.at("frames").size()) return false;
    std::map<std::string, Image> pages;
    size_t compared = 0;
    auto page = [&](const fs::path& dir, const std::string& leaf) -> Image& {
        auto key = (dir / leaf).string();
        if (!pages.count(key)) pages[key] = load_png(dir / leaf, 4);
        return pages[key];
    };
    for (size_t i = 0; i < a.at("frames").size(); ++i)
        for (auto& [usage, x] : a.at("frames")[i].at("usages").items()) {
            if (!b.at("frames")[i].at("usages").contains(usage)) continue;  // only usages the variant was built for
            ++compared;
            auto& y = b.at("frames")[i].at("usages").at(usage);
            if (x.value("pixel_sha256", "") == y.value("pixel_sha256", "")) continue;
            auto& r = x.at("rect");
            auto& s = y.at("rect");
            if (r[2] != s[2] || r[3] != s[3]) return false;
            auto p = crop(page(base, x.at("atlas")), r[0], r[1], r[2], r[3]), q = crop(page(variant, y.at("atlas")), s[0], s[1], s[2], s[3]);
            double sum = 0; size_t off = 0, n = p.px.size() / 4;  // n drops for skipped pixels
            for (size_t k = 0; k < p.px.size(); k += 4) {
                // Transparent pixels keep arbitrary extended RGB; only what is drawn counts.
                if (!p.px[k + 3] && !q.px[k + 3]) { --n; continue; }
                if (std::abs(int(p.px[k + 3]) - int(q.px[k + 3])) > 128) { ++off; continue; }
                int worst = 0;
                for (int c = 0; c < 3; ++c) { int d = std::abs(int(p.px[k + c]) - int(q.px[k + c])); sum += d; worst = std::max(worst, d); }
                if (worst > 24) ++off;
            }
            if (n && (sum / (3.0 * n) >= 1.0 || off * 200 > n)) return false;
        }
    return compared > 0;
}

// Remastered variants from the packs alone (also used when updating an installed game):
// animations get graded copies of the Original variant, static redraws a copy of the redraw when the palette change is invisible.
void build_remastered_variants(const RemasteredPaths& paths, const json& specs) {
    for (auto& spec : specs) {
        check_cancelled();
        std::string name = spec.at("resource"), identity = spec.at("palette_sha256");
        auto source = paths.original / "variants" / identity / "flc" / name;
        auto base = paths.output / "flc" / name;
        if (!fs::exists(source / "index.json") || !fs::exists(base / "index.json")) continue;
        auto base_index = read_json(base / "index.json");
        auto target = paths.output / "variants" / identity / "flc" / name;
        if (base_index.at("frame_count").get<int>() > 1 && base_index.contains("grade_rgb_gain")) {
            auto& gain = base_index.at("grade_rgb_gain");
            std::array<double, 3> gains{gain[0].get<double>(), gain[1].get<double>(), gain[2].get<double>()};
            auto index = read_json(source / "index.json");
            remove_tree(target);
            fs::create_directories(target);
            std::map<std::string, Image> graded;
            for (auto& atlas : index.at("atlases")) {
                std::string leaf = atlas.at("path");
                auto page = grade(load_png(source / leaf, 4), gains);
                save_png(page, target / leaf);
                atlas["sha256"] = sha_file(target / leaf);
                graded[leaf] = std::move(page);
            }
            for (auto& frame : index.at("frames"))
                for (auto& [usage, asset] : frame.at("usages").items()) {
                    auto& r = asset.at("rect");
                    asset["pixel_sha256"] = sha_bytes(crop(graded.at(asset.at("atlas").get<std::string>()), r[0], r[1], r[2], r[3]).px);
                }
            index["mode"] = "Remastered";
            index["grade_rgb_gain"] = gain;
            write_json(target / "index.json", index);
        } else if (!base_index.contains("grade_rgb_gain")) {  // a redraw, static or animated
            if (!palette_invisible(paths.original / "flc" / name, source)) continue;
            remove_tree(target);
            fs::create_directories(target);
            for (auto& atlas : base_index.at("atlases")) fs::copy_file(base / atlas.at("path").get<std::string>(), target / atlas.at("path").get<std::string>());
            for (auto& frame : base_index.at("frames")) frame["palette_sha256"] = identity;
            base_index["static_redraw_palette_copy"] = true;
            write_json(target / "index.json", base_index);
        } else continue;
        update_variant_catalog(paths.output, identity, name);
        paths.log("[detail] Remaster " + name + " v paletě " + identity.substr(0, 8));
    }
}
}
