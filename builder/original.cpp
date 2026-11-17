#include "original.h"
#include "data/hash.h"
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace hb {
static json parameters() {
    return {{"model", "realesr-animevideov3"}, {"scale", SCALE}, {"gpu", 0}, {"tile", 128}, {"load_proc_save", "2:2:2"},
            {"tta", false}, {"reflect_padding", PAD}, {"alpha_resample", "nearest"}, {"alpha_threshold", 128},
            {"overlay_rgb", "nearest visible source-index color extension"}, {"hidden_rgb", 0}, {"atlas_limit", 4096},
            {"atlas_gutter", 2}, {"font_alpha", "threshold model-upscaled source bitmap at 128"}, {"implementation", "native builder 1"}};
}

template <class F> static void parallel(size_t count, int workers, F f) {
    std::atomic<size_t> next{0};
    std::exception_ptr failure;
    std::mutex lock;
    std::vector<std::thread> threads;
    for (int w = 0; w < workers; ++w)
        threads.emplace_back([&] {
            for (size_t i; (i = next++) < count;) {
                try { f(i); } catch (...) { std::lock_guard g(lock); if (!failure) failure = std::current_exception(); next = count; }
            }
        });
    for (auto& t : threads) t.join();
    if (failure) std::rethrow_exception(failure);
}

std::pair<std::vector<std::array<int, 2>>, std::vector<std::array<int, 5>>> atlas_layout(const std::vector<std::array<int, 2>>& sizes) {
    std::vector<std::array<int, 2>> pages;
    std::vector<std::array<int, 5>> placements;
    int x = 0, y = 0, row = 0, used_w = 0, used_h = 0, page = 0;
    for (auto [w, h] : sizes) {
        if (x + w + 4 > 4096) { x = 0; y += row; row = 0; }
        if (y + h + 4 > 4096) { pages.push_back({used_w, used_h}); ++page; x = y = row = used_w = used_h = 0; }
        placements.push_back({page, x + 2, y + 2, w, h});
        used_w = std::max(used_w, x + w + 4); used_h = std::max(used_h, y + h + 4);
        row = std::max(row, h + 4);
        x += w + 4;
    }
    if (!sizes.empty()) pages.push_back({used_w, used_h});
    return {pages, placements};
}

Image extruded(const Image& image) { return pad_edge(image, 2); }

Image extend_rgb(const Image& rgb, const std::vector<uint8_t>& mask) {
    bool any = false, all = true;
    for (auto m : mask) { any |= m != 0; all &= m != 0; }
    if (all || !any) return rgb;
    auto nearest = nearest_set(mask, rgb.w, rgb.h);
    Image out(rgb.w, rgb.h, 3);
    for (size_t i = 0; i < nearest.size(); ++i) std::memcpy(&out.px[i * 3], &rgb.px[size_t(nearest[i]) * 3], 3);
    return out;
}

void run_upscaler(const Upscaler& u, const fs::path& in, const fs::path& out, const fs::path& log, const char* jobs) {
    int code = run_process({u.runner.string(), "-i", in.string(), "-o", out.string(), "-m", u.models.string(), "-n", u.model_name,
                            "-s", "3", "-t", "128", "-g", "0", "-j", jobs, "-f", "png"}, log);
    if (code != 0) throw UserError("Zvětšování selhalo (kód " + std::to_string(code) + "). Podrobnosti jsou v protokolu " + log.string());
}

namespace {
struct Frame {
    Image indices;                 // crop-sized gray, empty when the crop is empty
    std::vector<uint8_t> restored; // 640x480 indices
    std::vector<std::array<uint8_t, 3>> palette;
};

struct Original {
    const OriginalPaths& p;
    const Upscaler& u;
    json params = parameters();
    std::string config;

    json artifact(const fs::path& path) const {
        return {{"file", path.string()}, {"sha256", sha_file(path)}, {"bytes", fs::file_size(path)}};
    }
    bool valid_artifact(const json& a) const {
        fs::path path = a.at("file").get<std::string>();
        return fs::is_regular_file(path) && sha_file(path) == a.at("sha256").get<std::string>();
    }

    Frame load_frame(const std::string& name, const json& f) const {
        Frame out;
        for (auto& c : f.at("palette")) out.palette.push_back({c[0].get<uint8_t>(), c[1].get<uint8_t>(), c[2].get<uint8_t>()});
        auto& c = f.at("crop");
        int x = c.at("x"), y = c.at("y"), w = c.at("width"), h = c.at("height");
        out.restored.assign(640 * 480, 0);
        if (!c.at("empty").get<bool>()) {
            auto& record = f.at(".indices.png");
            auto path = p.source / name / record.at("path").get<std::string>();
            if (sha_file(path) != record.at("sha256").get<std::string>()) throw UserError("Vybalený snímek se změnil: " + name);
            out.indices = load_png(path, 1);
            if (out.indices.w != w || out.indices.h != h) throw UserError("Vybalený snímek má jinou velikost: " + name);
            for (int yy = 0; yy < h; ++yy) std::memcpy(&out.restored[size_t(y + yy) * 640 + x], out.indices.at(0, yy), w);
        }
        if (sha_bytes(out.restored.data(), out.restored.size()) != f.at("indices_sha256").get<std::string>())
            throw UserError("Vybalený snímek nesedí s kontrolním součtem: " + name);
        return out;
    }

    Image palette_rgb(const std::vector<uint8_t>& indices, int w, int h, const Frame& f) const {
        Image out(w, h, 3);
        for (size_t i = 0; i < indices.size(); ++i) std::memcpy(&out.px[i * 3], f.palette[indices[i]].data(), 3);
        return out;
    }

    fs::path model_folder(const std::string& key) const { return p.cache / "model" / key.substr(0, 2); }

    bool model_ready(const std::string& key) const {
        try {
            auto r = read_json(model_folder(key) / (key + ".json"));
            return r.at("key") == key && valid_artifact(r.at("input")) && valid_artifact(r.at("output"));
        } catch (...) { return false; }
    }

    // Returns the cache key; schedules inference into `jobs` when the cache lacks it.
    std::string prepare(const Image& rgb, std::map<std::string, std::array<int, 2>>& jobs, std::mutex& lock) const {
        auto padded = pad_reflect101(rgb, PAD);
        std::string header = "hb-model|" + std::to_string(padded.w) + "x" + std::to_string(padded.h) + "|" + u.runner_sha256 + "|" + params.dump();
        hl::Sha256 hash;
        hash.update(header.data(), header.size());
        hash.update(padded.px.data(), padded.px.size());
        auto key = hash.hex();
        {
            // Reserve the key first: identical frames in one batch must not write the same file twice.
            std::lock_guard g(lock);
            if (jobs.count(key) || model_ready(key)) return key;
            jobs[key] = {padded.w * SCALE, padded.h * SCALE};
        }
        save_png(padded, model_folder(key) / (key + ".input.png"));
        return key;
    }

    void infer(const std::map<std::string, std::array<int, 2>>& jobs, const std::string& label) const {
        if (jobs.empty()) return;
        auto staging = p.cache / "batch";
        remove_tree(staging);
        fs::create_directories(staging / "in");
        fs::create_directories(staging / "out");
        for (auto& [key, size] : jobs) {
            std::error_code ec;
            auto input = model_folder(key) / (key + ".input.png");
            fs::create_hard_link(input, staging / "in" / (key + ".png"), ec);
            if (ec) fs::copy_file(input, staging / "in" / (key + ".png"));
        }
        auto log = p.evidence / ("model-" + label + ".log");
        run_upscaler(u, staging / "in", staging / "out", log);
        for (auto& [key, size] : jobs) {
            auto output = staging / "out" / (key + ".png");
            auto check = load_png(output, 3);
            if (check.w != size[0] || check.h != size[1]) throw UserError("Zvětšovač vrátil obrázek špatné velikosti.");
            auto target = model_folder(key) / (key + ".png");
            fs::rename(output, target);
            write_json(model_folder(key) / (key + ".json"), {{"key", key},
                {"input", artifact(model_folder(key) / (key + ".input.png"))}, {"output", artifact(target)}, {"size", {size[0], size[1]}}});
        }
        remove_tree(staging);
    }

    Image raw(const std::string& key, int w, int h) const {
        auto image = load_png(model_folder(key) / (key + ".png"), 3);
        return crop(image, PAD * SCALE, PAD * SCALE, w * SCALE, h * SCALE);
    }

    static Image finish(const Image& rgb, const std::vector<uint8_t>& mask, int mw, int mh) {
        Image out(rgb.w, rgb.h, 4);
        for (int y = 0; y < rgb.h; ++y)
            for (int x = 0; x < rgb.w; ++x) {
                bool on = mask[size_t(y * mh / rgb.h) * mw + x * mw / rgb.w] >= 128;
                uint8_t* d = out.at(x, y);
                if (on) { std::memcpy(d, rgb.at(x, y), 3); d[3] = 255; }
            }
        return out;
    }

    fs::path record_path(const std::string& name, int index) const {
        return p.cache / "frames" / name / ("frame-" + std::to_string(index) + ".json");
    }

    bool reusable_frame(const json& resource, const json& f) const {
        try {
            auto r = read_json(record_path(resource.at("name"), f.at("index")));
            if (r.at("config_sha256") != config || r.at("source_sha256") != resource.at("source_sha256") ||
                r.at("frame_sha256") != sha_text(f.dump())) return false;
            for (auto& [usage, a] : r.at("outputs").items()) {
                if (!valid_artifact(a)) return false;
                if (!a.at("model_key").is_null() && !model_ready(a.at("model_key"))) return false;
            }
            return true;
        } catch (...) { return false; }
    }

    void render(const json& resource, const json& f, const std::string& base_key, const std::string& overlay_key) const {
        std::string name = resource.at("name");
        auto frame = load_frame(name, f);
        auto& c = f.at("crop");
        int w = c.at("width"), h = c.at("height");
        json outputs;
        auto folder = p.cache / "frames" / name;
        for (int usage = 0; usage < 2; ++usage) {
            std::string label = usage ? "overlay" : "opaque_base";
            const std::string& key = usage ? overlay_key : base_key;
            Image image;
            if (key.empty()) image = Image(3, 3, 4);
            else if (!usage) image = finish(raw(key, 640, 480), std::vector<uint8_t>(640 * 480, 255), 640, 480);
            else {
                std::vector<uint8_t> mask(frame.indices.px.size());
                for (size_t i = 0; i < mask.size(); ++i) mask[i] = frame.indices.px[i] ? 255 : 0;
                image = finish(raw(key, w, h), mask, w, h);
            }
            auto path = folder / ("frame-" + std::to_string(f.at("index").get<int>()) + "." + label + ".png");
            save_png(image, path);
            auto a = artifact(path);
            a["model_key"] = key.empty() ? json(nullptr) : json(key);
            a["size"] = {image.w, image.h};
            a["pixel_sha256"] = sha_bytes(image.px);
            outputs[label] = a;
        }
        write_json(record_path(name, f.at("index")), {{"resource", name}, {"frame", f.at("index")},
            {"source_sha256", resource.at("source_sha256")}, {"frame_sha256", sha_text(f.dump())},
            {"config_sha256", config}, {"outputs", outputs}});
    }

    json build_atlases(const json& resource, const json& manifest) const {
        std::string name = resource.at("name");
        auto target = p.out / "flc" / name;
        fs::create_directories(target);
        std::vector<json> records, frames;
        for (auto& f : manifest.at("frames")) {
            records.push_back(read_json(record_path(name, f.at("index"))));
            json e = f;
            std::string palette;
            for (auto& rgb : f.at("palette")) for (auto& v : rgb) palette.push_back(char(v.get<int>()));
            e["frame"] = f.at("index"); e["resource"] = name; e["logical_offset"] = f.at("offset");
            e["physical_offset"] = {f.at("offset")[0].get<int>() * SCALE, f.at("offset")[1].get<int>() * SCALE};
            e["usages"] = json::object(); e["palette_sha256"] = sha_text(palette);
            json pngs;
            for (auto k : {".png", ".indices.png", ".mask.png"}) if (f.contains(k)) { pngs[k] = f.at(k); e.erase(k); }
            e["source_pngs"] = pngs;
            frames.push_back(e);
        }
        json atlases = json::array();
        for (std::string usage : {"overlay", "opaque_base"}) {
            std::vector<std::array<int, 2>> sizes;
            for (auto& r : records) sizes.push_back({r["outputs"][usage]["size"][0], r["outputs"][usage]["size"][1]});
            auto [pages, places] = atlas_layout(sizes);
            for (size_t page = 0; page < pages.size(); ++page) {
                Image atlas(pages[page][0], pages[page][1], 4);
                for (size_t i = 0; i < places.size(); ++i) {
                    auto [pg, x, y, w, h] = places[i];
                    if (size_t(pg) != page) continue;
                    auto& out = records[i]["outputs"][usage];
                    paste(atlas, extruded(load_png(out["file"].get<std::string>(), 4)), x - 2, y - 2);
                    bool overlay = usage == "overlay";
                    auto& crop = frames[i]["crop"];
                    frames[i]["usages"][usage] = {
                        {"atlas", usage + "-" + std::to_string(page) + ".png"}, {"rect", {x, y, w, h}},
                        {"crop", overlay ? crop : json{{"x", 0}, {"y", 0}, {"width", 640}, {"height", 480}, {"empty", false}}},
                        {"logical_offset", overlay ? frames[i]["offset"] : json{0, 0}},
                        {"output_sha256", out["sha256"]}, {"pixel_sha256", out["pixel_sha256"]},
                        {"model_key", out["model_key"]}, {"empty", overlay ? crop["empty"] : json(false)}};
                }
                auto path = target / (usage + "-" + std::to_string(page) + ".png");
                save_png(atlas, path);
                auto a = artifact(path);
                a["path"] = path.filename().string(); a["size"] = {pages[page][0], pages[page][1]}; a["usage"] = usage;
                atlases.push_back(a);
            }
        }
        json result = {{"schema_version", 1}, {"mode", "Original"}, {"resource", name},
            {"source_sha256", resource.at("source_sha256")}, {"source_manifest_sha256", sha_file(p.source / resource.at("manifest").get<std::string>())},
            {"logical_canvas", {640, 480}}, {"physical_canvas", {1920, 1440}}, {"scale", SCALE}, {"frame_count", frames.size()},
            {"palette_variant", "source-frame-palette"}, {"model", u.model}, {"parameters", params}, {"config_sha256", config},
            {"usage_selection", "Use opaque_base only for first full-screen base-copy draw; otherwise overlay."},
            {"transparent_index", 0}, {"atlas_gutter", 2}, {"atlases", atlases}, {"frames", frames}};
        write_json(target / "index.json", result);
        return result;
    }

    bool reusable_atlases(const json& resource) const {
        try {
            auto r = read_json(p.out / "flc" / resource.at("name").get<std::string>() / "index.json");
            if (r.at("config_sha256") != config || r.at("frame_count") != resource.at("frames") ||
                r.at("source_manifest_sha256") != sha_file(p.source / resource.at("manifest").get<std::string>())) return false;
            for (auto& a : r.at("atlases")) if (!valid_artifact(a)) return false;
            return true;
        } catch (...) { return false; }
    }

    void build_fonts() const {
        auto data = read_file(p.source.parent_path() / "font" / "original-font.bin");
        auto meta = read_json(p.source.parent_path() / "font" / "glyphs.json");
        if (data.size() != 4096 || meta.at("glyph_count") != 256) throw UserError("Písmo hry má neočekávaný tvar.");
        std::map<std::string, std::array<int, 2>> jobs;
        std::mutex lock;
        std::vector<std::string> keys;
        for (int i = 0; i < 256; ++i) {
            Image rgb(8, 16, 3);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 8; ++x)
                    if (data[i * 16 + y] >> (7 - x) & 1) std::memset(rgb.at(x, y), 255, 3);
            keys.push_back(prepare(rgb, jobs, lock));
        }
        infer(jobs, "font");
        Image sheet(16 * 28, 16 * 52, 4);
        json glyphs = json::array();
        for (int i = 0; i < 256; ++i) {
            auto gray = convert(raw(keys[i], 8, 16), 1);
            Image glyph(24, 48, 4);
            for (size_t k = 0; k < gray.px.size(); ++k) if (gray.px[k] >= 128) std::memset(&glyph.px[k * 4], 255, 4);
            int x = (i % 16) * 28 + 2, y = (i / 16) * 52 + 2;
            paste(sheet, extruded(glyph), x - 2, y - 2);
            glyphs.push_back({{"index", i}, {"codepoint", meta.at("glyphs")[i].at("codepoint")}, {"rect", {x, y, 24, 48}},
                {"model_key", keys[i]}, {"source_sha256", sha_bytes(&data[i * 16], 16)}, {"output_pixel_sha256", sha_bytes(glyph.px)},
                {"logical_size", {8, 16}}, {"size", {24, 48}}, {"advance", 24}, {"baseline", 36}});
        }
        auto path = p.out / "font" / "glyphs.png";
        save_png(sheet, path);
        write_json(p.out / "font" / "index.json", {{"schema_version", 1}, {"encoding", "Kamenicky (DOS CP895 alias, not the unrelated IBM-895)"},
            {"source_sha256", sha_bytes(data)}, {"model", u.model}, {"parameters", params}, {"config_sha256", config},
            {"atlas", artifact(path)}, {"glyph_count", 256}, {"logical_advance", 8}, {"logical_baseline", 12},
            {"logical_anchor", "top-left"}, {"logical_size", {8, 16}}, {"size", {24, 48}}, {"advance", 24}, {"baseline", 36},
            {"glyphs", glyphs}});
    }

    // A frame drawn while the game shows another palette (menu cursor, inventory under a scene palette...).
    // The engine looks these up as RESOURCE@palette in packs/<mode>/variants.
    void build_variant(const json& spec, const json& manifest, const std::vector<std::array<uint8_t, 3>>& palette,
                       const std::string& identity) const {
        std::string name = spec.at("resource");
        auto base_dir = p.out / "flc" / name;
        auto original = read_json(base_dir / "index.json");
        auto target = p.out / "variants" / identity / "flc" / name;
        std::string variant_config = sha_text(config + "|" + spec.dump() + "|" + original.at("config_sha256").get<std::string>());
        try {
            auto done = read_json(target / "index.json");
            bool ok = done.at("variant_config_sha256") == variant_config;
            for (auto& a : done.at("atlases")) ok = ok && fs::is_regular_file(target / a.at("path").get<std::string>()) &&
                                                   sha_file(target / a.at("path").get<std::string>()) == a.at("sha256").get<std::string>();
            if (ok) return;
        } catch (...) {}
        remove_tree(target);
        fs::create_directories(target);
        std::vector<std::string> usages;
        for (auto& u : spec.at("usages")) usages.push_back(u);
        std::map<std::string, Image> pages;  // original atlases, loaded on demand
        auto original_tile = [&](const json& asset) {
            std::string leaf = asset.at("atlas");
            if (!pages.count(leaf)) pages[leaf] = load_png(base_dir / leaf, 4);
            auto& r = asset.at("rect");
            return crop(pages[leaf], r[0], r[1], r[2], r[3]);
        };
        struct Tile { Image image; std::string key; };
        std::map<std::string, std::vector<Tile>> tiles;
        auto& frames = manifest.at("frames");
        for (size_t start = 0; start < frames.size(); start += 16) {
            size_t end = std::min(frames.size(), start + 16);
            std::map<std::string, std::array<int, 2>> jobs;
            std::mutex lock;
            struct Pending { std::string usage, key; std::vector<uint8_t> mask; int w = 0, h = 0; Image ready; };
            std::vector<Pending> batch;
            for (size_t i = start; i < end; ++i) {
                auto& f = frames[i];
                auto frame = load_frame(name, f);
                int index = f.at("index");
                for (auto& usage : usages) {
                    bool opaque = usage == "opaque_base";
                    Pending item{usage};
                    if (!opaque && frame.indices.empty()) { item.ready = Image(3, 3, 4); batch.push_back(std::move(item)); continue; }
                    const std::vector<uint8_t>& raster = opaque ? frame.restored : frame.indices.px;
                    item.w = opaque ? 640 : frame.indices.w;
                    item.h = opaque ? 480 : frame.indices.h;
                    bool same = true;
                    for (auto v : raster) if ((opaque || v) && frame.palette[v] != palette[v]) { same = false; break; }
                    if (same) {
                        item.ready = original_tile(original.at("frames").at(index).at("usages").at(usage));
                        batch.push_back(std::move(item));
                        continue;
                    }
                    Image rgb(item.w, item.h, 3);
                    for (size_t k = 0; k < raster.size(); ++k) std::memcpy(&rgb.px[k * 3], palette[raster[k]].data(), 3);
                    item.mask.assign(raster.size(), 255);
                    if (!opaque) {
                        std::vector<uint8_t> visible(raster.size());
                        for (size_t k = 0; k < raster.size(); ++k) visible[k] = raster[k] != 0, item.mask[k] = raster[k] ? 255 : 0;
                        rgb = extend_rgb(rgb, visible);
                    }
                    item.key = prepare(rgb, jobs, lock);
                    batch.push_back(std::move(item));
                }
            }
            infer(jobs, "variant-" + name + "-" + std::to_string(start));
            for (auto& item : batch) {
                if (!item.key.empty()) item.ready = finish(raw(item.key, item.w, item.h), item.mask, item.w, item.h);
                tiles[item.usage].push_back({std::move(item.ready), item.key});
            }
        }
        json result = original;
        result["atlases"] = json::array();
        std::string palette_json;
        json palette_list = json::array();
        for (auto& c : palette) palette_list.push_back({c[0], c[1], c[2]});
        for (auto& frame : result.at("frames")) {
            frame["original_palette_sha256"] = frame.at("palette_sha256");
            frame["palette_sha256"] = identity;
            frame["palette"] = palette_list;
            frame["usages"] = json::object();
        }
        for (auto& usage : usages) {
            auto& list = tiles[usage];
            std::vector<std::array<int, 2>> sizes;
            for (auto& t : list) sizes.push_back({t.image.w, t.image.h});
            auto [layout_pages, places] = atlas_layout(sizes);
            for (size_t page = 0; page < layout_pages.size(); ++page) {
                Image atlas(layout_pages[page][0], layout_pages[page][1], 4);
                std::string leaf = usage + "-" + std::to_string(page) + ".png";
                for (size_t i = 0; i < places.size(); ++i) {
                    auto [pg, x, y, w, h] = places[i];
                    if (size_t(pg) != page) continue;
                    paste(atlas, extruded(list[i].image), x - 2, y - 2);
                    auto& frame = result["frames"][i];
                    bool overlay = usage == "overlay";
                    frame["usages"][usage] = {{"atlas", leaf}, {"rect", {x, y, w, h}},
                        {"logical_offset", overlay ? frame.at("logical_offset") : json{0, 0}},
                        {"empty", overlay ? frame.at("crop").at("empty") : json(false)},
                        {"model_key", list[i].key.empty() ? json(nullptr) : json(list[i].key)},
                        {"pixel_sha256", sha_bytes(list[i].image.px)}};
                }
                save_png(atlas, target / leaf);
                result["atlases"].push_back({{"path", leaf}, {"size", {layout_pages[page][0], layout_pages[page][1]}},
                                             {"sha256", sha_file(target / leaf)}, {"usage", usage}});
            }
        }
        result["variant_config_sha256"] = variant_config;
        result["offline_palette_variant"] = {{"resource", name}, {"palette_source", spec.at("palette_source")},
                                             {"palette_sha256", identity}, {"usages", spec.at("usages")}};
        write_json(target / "index.json", result);
    }
};

}  // namespace

void update_variant_catalog(const fs::path& pack, const std::string& identity, const std::string& name) {
    auto path = pack / "variants" / "index.json";
    json catalog = fs::exists(path) ? read_json(path) : json{{"schema_version", 1}, {"variants", json::array()}};
    json* row = nullptr;
    for (auto& r : catalog.at("variants")) if (r.at("palette_sha256") == identity) row = &r;
    if (!row) { catalog["variants"].push_back({{"palette_sha256", identity}, {"path", identity}, {"resources", json::array()}}); row = &catalog["variants"].back(); }
    std::set<std::string> names;
    for (auto& r : row->at("resources")) names.insert(r.get<std::string>());
    names.insert(name);
    (*row)["resources"] = json(std::vector<std::string>(names.begin(), names.end()));
    write_json(path, catalog);
}

void build_palette_variants(const OriginalPaths& paths, const Upscaler& upscaler, const json& resources, const json& specs,
                            const std::function<void(int, int, const std::string&)>& progress) {
    Original o{paths, upscaler};
    o.config = sha_text("hb-original|" + upscaler.runner_sha256 + "|" + o.params.dump());
    std::map<std::string, json> by_name;
    for (auto& r : resources) by_name[r.at("name")] = r;
    auto manifest_of = [&](const std::string& name) {
        auto it = by_name.find(name);
        if (it == by_name.end()) throw UserError("Chybí obrázek pro barevnou variantu: " + name);
        return read_json(paths.source / it->second.at("manifest").get<std::string>());
    };
    int done = 0, total = int(specs.size());
    for (auto& spec : specs) {
        check_cancelled();
        std::string name = spec.at("resource"), source = spec.at("palette_source");
        if (!selected_resource(name)) { progress(++done, total, name); continue; }
        std::vector<std::array<uint8_t, 3>> palette;
        std::string bytes;
        auto palette_manifest = manifest_of(source);  // keep alive: the loop below borrows from it
        for (auto& c : palette_manifest.at("frames").at(0).at("palette")) {
            palette.push_back({c[0].get<uint8_t>(), c[1].get<uint8_t>(), c[2].get<uint8_t>()});
            for (int k = 0; k < 3; ++k) bytes.push_back(char(c[k].get<int>()));
        }
        auto identity = sha_bytes(bytes.data(), bytes.size());
        if (palette.size() != 256 || identity != spec.at("palette_sha256").get<std::string>())
            throw UserError("Paleta pro barevnou variantu nesedí: " + source);
        o.build_variant(spec, manifest_of(name), palette, identity);
        update_variant_catalog(paths.out, identity, name);
        progress(++done, total, name);
    }
}

void build_original(const OriginalPaths& paths, const Upscaler& upscaler, const json& resources,
                    const std::function<void(int, int, const std::string&)>& progress) {
    Original o{paths, upscaler};
    o.config = sha_text("hb-original|" + upscaler.runner_sha256 + "|" + o.params.dump());
    fs::create_directories(paths.evidence);
    int done = 0, total = int(resources.size());
    for (auto& resource : resources) {
        check_cancelled();
        std::string name = resource.at("name");
        if (!selected_resource(name)) { progress(++done, total, name); continue; }
        auto manifest = read_json(paths.source / resource.at("manifest").get<std::string>());
        if (manifest.at("source_sha256") != resource.at("source_sha256") || manifest.at("frames").size() != resource.at("frames").get<size_t>())
            throw UserError("Vybalená data nesedí: " + name);
        std::vector<json> pending;
        for (auto& f : manifest.at("frames")) if (!o.reusable_frame(resource, f)) pending.push_back(f);
        for (size_t start = 0; start < pending.size(); start += 64) {
            size_t end = std::min(pending.size(), start + 64);
            std::map<std::string, std::array<int, 2>> jobs;
            std::mutex lock;
            std::vector<std::array<std::string, 2>> keys(end - start);
            parallel(end - start, 4, [&](size_t i) {
                auto& f = pending[start + i];
                auto frame = o.load_frame(name, f);
                keys[i][0] = o.prepare(o.palette_rgb(frame.restored, 640, 480, frame), jobs, lock);
                if (!frame.indices.empty()) {
                    std::vector<uint8_t> mask(frame.indices.px.size());
                    for (size_t k = 0; k < mask.size(); ++k) mask[k] = frame.indices.px[k] != 0;
                    keys[i][1] = o.prepare(extend_rgb(o.palette_rgb(frame.indices.px, frame.indices.w, frame.indices.h, frame), mask), jobs, lock);
                }
            });
            o.infer(jobs, name + "-" + std::to_string(start));
            parallel(end - start, 4, [&](size_t i) { o.render(resource, pending[start + i], keys[i][0], keys[i][1]); });
        }
        if (!pending.empty() || !o.reusable_atlases(resource)) o.build_atlases(resource, manifest);
        progress(++done, total, name);
    }
    if (selected_resource("font")) o.build_fonts();
}
}
