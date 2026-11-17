#include "pipeline.h"
#include "crypto.h"
#include "disc.h"
#include "remastered.h"
#include "zip.h"
#include <cstring>
#include <map>

namespace hb {
extern const char* const EMBEDDED_PINS;  // generated from launcher/pins.json

#ifdef _WIN32
constexpr bool WINDOWS = true;
#else
constexpr bool WINDOWS = false;
#endif

json builder_pins() { return json::parse(EMBEDDED_PINS); }
const char* app_name() { return WINDOWS ? "HorkeLeto" : "Horke Leto.app"; }

// The payload ZIP rides inside the builder: appended to the .exe on Windows, in Resources on macOS.
static Bytes embedded_payload() {
    auto exe = executable_path();
    if (!WINDOWS) {
        auto bundled = exe.parent_path().parent_path() / "Resources" / "payload.zip";
        if (fs::exists(bundled)) return read_file(bundled);
        auto beside = exe.parent_path() / "payload.zip";  // development builds
        if (fs::exists(beside)) return read_file(beside);
        throw UserError("Průvodci chybí přibalená data (payload.zip). Stáhni ho prosím znovu.");
    }
    auto self = read_file(exe);
    if (self.size() < 16 || std::memcmp(&self[self.size() - 8], "HLPAYLD1", 8))
        throw UserError("Průvodci chybí přibalená data. Stáhni ho prosím znovu.");
    uint64_t size = 0;
    for (int i = 0; i < 8; ++i) size |= uint64_t(self[self.size() - 16 + i]) << (8 * i);
    if (size > self.size() - 16) throw UserError("Přibalená data jsou poškozená. Stáhni průvodce prosím znovu.");
    return Bytes(self.end() - 16 - std::ptrdiff_t(size), self.end() - 16);
}

std::string self_check() {
    auto pins = builder_pins();
    auto payload = embedded_payload();
    auto entries = zip_entries(payload);
    bool tool = false, upscaler = false, game = false;
    for (auto& e : entries) {
        tool |= e.name == (WINDOWS ? "hl-tool.exe" : "hl-tool");
        upscaler |= e.name == "upscaler.zip";
        game |= e.name.rfind("game/", 0) == 0;
    }
    if (!tool || !upscaler || !game) throw UserError("Payload is incomplete");
    return "payload " + std::to_string(entries.size()) + " files, art " + pins.at("art").at("version").get<std::string>();
}

static fs::path extract_payload(const fs::path& work, const Emit& emit) {
    auto payload = embedded_payload();
    auto target = work / "tools";
    Checkpoint checkpoint(work / "tools-complete.json", sha_bytes(payload));
    if (!checkpoint.valid()) {
        emit("[detail] Rozbaluji nástroje průvodce");
        remove_tree(target);
        zip_extract(payload, target);
        checkpoint.commit(files_under(target));
    }
    return target;
}

void verify_data(const fs::path& data, const json& pins, bool strict_audio) {
    for (auto& [name, expected] : pins.at("files").items())
        if (!fs::is_regular_file(data / name) || sha_file(data / name) != expected.get<std::string>())
            throw UserError("Herní soubor chybí nebo nesedí: " + name);
    auto audio = read_json(data / "audio" / "manifest.json");
    std::map<std::string, json> tracks;
    for (auto& t : audio.at("tracks")) if (!t.value("pregap", false)) tracks[std::to_string(t.at("track").get<int>())] = t;
    for (auto& [number, pcm] : pins.at("audio").items()) {
        auto& row = tracks.at(number);
        if (sha_file(data / "audio" / row.at("path").get<std::string>()) != row.at("flac_sha256").get<std::string>() ||
            (strict_audio && row.at("source_pcm_sha256") != pcm))
            throw UserError("Hudba z CD nesedí (stopa " + number + ").");
    }
}

static void run_tool(const fs::path& tools, std::vector<std::string> args, const fs::path& log) {
    args.insert(args.begin(), (tools / (WINDOWS ? "hl-tool.exe" : "hl-tool")).string());
    int code = run_process(args, log);
    if (code != 0) throw UserError("Nástroj " + args[1] + " selhal (kód " + std::to_string(code) + "). Podrobnosti: " + log.string());
}

static void decrypt_art(const fs::path& archive, const fs::path& grp_path, const fs::path& destination, const Emit& emit) {
    auto zip = read_file(archive);
    std::map<std::string, ZipEntry> entries;
    for (auto& e : zip_entries(zip)) entries[e.name] = e;
    auto rows = json::parse(zip_read(zip, entries.at("manifest.json")));
    auto recipes = json::parse(zip_read(zip, entries.at("recipes.json")));
    // Original GRP members are the keys.
    auto grp = read_file(grp_path);
    if (grp.size() < 44 || std::memcmp(grp.data(), "AGDS group file\x1a", 16)) throw UserError("GFX.GRP má neplatnou hlavičku.");
    auto le = [&](size_t at) { return uint32_t(grp[at]) | grp[at + 1] << 8 | grp[at + 2] << 16 | uint32_t(grp[at + 3]) << 24; };
    std::map<std::string, std::pair<uint32_t, uint32_t>> members;
    for (uint32_t i = 0, used = le(28); i < used; ++i) {
        size_t at = 44 + size_t(i) * 30;
        std::string name(reinterpret_cast<char*>(&grp[at]), strnlen(reinterpret_cast<char*>(&grp[at]), 14));
        for (auto& c : name) c = char(toupper(c));
        members[name] = {le(at + 14), le(at + 18)};
    }
    auto stage = destination; stage += ".stage";
    remove_tree(stage);
    fs::create_directories(stage / "images");
    std::string salt_text = "Horke Leto Remastered art v1";
    Bytes salt(salt_text.begin(), salt_text.end());
    int done = 0, total = int(recipes.at("statics").size());
    for (auto& [name, row] : recipes.at("statics").items()) {
        check_cancelled();
        ++done;
        if (!row.contains("file")) continue;
        fs::path relative = row.at("file").get<std::string>();
        if (relative.parent_path() != "images" || relative.extension() != ".png") throw UserError("Neplatná cesta v balíčku překreseb.");
        Bytes plain;
        if (rows.contains(name)) {
            auto& meta = rows.at(name);
            auto member = members.find(name);
            if (member == members.end()) throw UserError("Na CD chybí obrázek " + name + ", klíč k překresbě.");
            Bytes entry(grp.begin() + member->second.first, grp.begin() + member->second.first + member->second.second);
            if (sha_bytes(entry) != meta.at("original_entry_sha256").get<std::string>()) throw UserError("Obrázek " + name + " na CD nesedí.");
            auto sealed = zip_read(zip, entries.at("encrypted/" + name + ".enc"));
            if (sha_bytes(sealed) != meta.at("ciphertext_sha256").get<std::string>()) throw UserError("Šifrovaná překresba je poškozená: " + name);
            std::string context = std::string("FLC redraw") + '\0' + name;
            Bytes aad(context.begin(), context.end());
            auto key = hkdf32(entry, salt, aad);
            Bytes nonce(24);
            auto hex = meta.at("nonce").get<std::string>();
            for (size_t i = 0; i < 24; ++i) nonce[i] = uint8_t(std::stoi(hex.substr(2 * i, 2), nullptr, 16));
            if (!xchacha20poly1305_decrypt(plain, sealed, aad, nonce.data(), key.data())) throw UserError("Překresbu " + name + " se nepodařilo odemknout.");
        } else {
            throw UserError("V balíčku chybí překresba " + name + ".");
        }
        if (sha_bytes(plain) != row.at("sha256").get<std::string>()) throw UserError("Překresba nesedí s kontrolním součtem: " + name);
        write_file(stage / relative, plain.data(), plain.size());
        emit("[progress] decrypt " + std::to_string(done) + " " + std::to_string(total) + " " + (stage / relative).string());
    }
    write_json(stage / "manifest.json", recipes);
    remove_tree(destination);
    fs::rename(stage, destination);
}

// Engine runtime expects binary alpha in Remastered atlases.
static void normalize_folder(const fs::path& flc);
static void normalize_runtime(const fs::path& pack) {
    normalize_folder(pack / "flc");
    std::error_code ec;
    if (fs::is_directory(pack / "variants", ec))
        for (auto& v : fs::directory_iterator(pack / "variants")) if (fs::is_directory(v.path() / "flc")) normalize_folder(v.path() / "flc");
}
static void normalize_folder(const fs::path& flc) {
    for (auto& entry : fs::directory_iterator(flc)) {
        auto path = entry.path() / "index.json";
        if (!fs::exists(path)) continue;
        auto index = read_json(path);
        std::map<std::string, Image> changed;
        for (auto& atlas : index.at("atlases")) {
            auto file = entry.path() / atlas.at("path").get<std::string>();
            auto image = load_png(file, 4);
            bool partial = false;
            for (size_t i = 3; i < image.px.size(); i += 4) if (image.px[i] && image.px[i] != 255) { partial = true; break; }
            if (!partial) continue;
            for (size_t i = 3; i < image.px.size(); i += 4) image.px[i] = image.px[i] >= 128 ? 255 : 0;
            save_png(image, file);
            atlas["sha256"] = sha_file(file);
            atlas["bytes"] = fs::file_size(file);
            changed[atlas.at("path")] = std::move(image);
        }
        if (changed.empty()) continue;
        for (auto& frame : index.at("frames"))
            for (auto& [name, usage] : frame.at("usages").items()) {
                auto it = changed.find(usage.at("atlas"));
                if (it == changed.end()) continue;
                auto& r = usage.at("rect");
                usage["pixel_sha256"] = sha_bytes(crop(it->second, r[0], r[1], r[2], r[3]).px);
            }
        index["runtime_alpha_threshold"] = 128;
        write_json(path, index);
    }
}

static void move_or_copy(const fs::path& from, const fs::path& to, bool move) {
    std::error_code ec;
    if (move) { fs::rename(from, to, ec); if (!ec) return; }
    fs::copy(from, to, fs::copy_options::recursive);
}

static fs::path assemble(const fs::path& tools, const fs::path& work, const fs::path& destination, bool move, const Emit& emit) {
    auto target = destination / app_name();
    auto stage = target; stage += ".stage";
    if (fs::exists(target)) throw UserError("V cílové složce už hra je; vyber jinou složku.");
    remove_tree(stage);
    fs::copy(tools / "game", stage, fs::copy_options::recursive);
    auto resources = WINDOWS ? stage / "Resources" : stage / "Contents" / "Resources";
    fs::create_directories(resources);
    emit("Kopírujeme data a balíčky do hry");
    move_or_copy(work / "data", resources / "data", move);
    move_or_copy(work / "packs", resources / "packs", move);
    normalize_runtime(resources / "packs" / "remastered");
    write_json(resources / "launch.json", {{"schema_version", 1}, {"data", "data"}, {"packs", "packs"}});
    try {
        make_game_icon(work, stage);
        emit("[detail] Ikona hry vytvořena z tvých herních dat");
    } catch (const std::exception& error) {
        emit(std::string("[detail] Ikonu hry se nepodařilo vytvořit: ") + error.what());
    }
    if (!WINDOWS) {
        // Nested code first, then the bundle (ad hoc signature, as macOS requires on Apple Silicon).
        for (auto& p : files_under(stage)) {
            auto perms = fs::status(p).permissions();
            if (p.extension() == ".dylib" || (perms & fs::perms::owner_exec) != fs::perms::none)
                run_process({"/usr/bin/codesign", "--force", "--sign", "-", p.string()});
        }
        if (run_process({"/usr/bin/codesign", "--force", "--sign", "-", stage.string()}) != 0 ||
            run_process({"/usr/bin/codesign", "--verify", "--deep", "--strict", stage.string()}) != 0)
            throw UserError("Podepsání hry selhalo.");
    }
    fs::rename(stage, target);
    emit("Hotovo: " + target.string());
    return target;
}

fs::path build_game(const BuildRequest& req, const Emit& emit) {
    auto pins = builder_pins();
    auto destination = fs::absolute(req.output);
    auto work = destination / ".builder";
    if (fs::exists(destination / app_name())) throw UserError("V cílové složce už hra je; vyber jinou složku.");
    fs::create_directories(work);
    KeepAwake awake;
    auto tools = extract_payload(work, emit);
    auto previous = fs::current_path();
    fs::current_path(tools);  // the importer finds its FLAC encoder relative to here
    struct Restore { fs::path p; ~Restore() { std::error_code ec; fs::current_path(p, ec); } } restore{previous};

    emit("[stage] read");
    emit("Ověřujeme tvoji zálohu");
    bool strict_audio = false;  // rips differ in audio offsets; game files are checked exactly
    auto discs = disc_pins(pins);
    fs::path cue = req.drive.empty() ? normalize_disc(req.image, work / "disc", discs, emit)
                                     : rip_drive(req.drive, work / "disc", discs, emit);
    Checkpoint imported(work / "import-complete.json", sha_file(work / "disc" / "disc.bin") + sha_text(pins.dump()));
    if (!imported.valid()) {
        auto stage = work / "data.stage";
        remove_tree(stage);
        run_tool(tools, {"import", "--cue", cue.string(), "--out", stage.string(), "--report", (work / "import-report.json").string(), "--rip-audio"},
                 work / "import.log");
        auto manifest_path = stage / "audio" / "manifest.json";
        auto audio = read_json(manifest_path);
        for (auto& row : audio.at("tracks")) row["flac_sha256"] = sha_file(stage / "audio" / row.at("path").get<std::string>());
        write_json(manifest_path, audio);
        verify_data(stage, pins, strict_audio);
        remove_tree(work / "data");
        fs::rename(stage, work / "data");
        imported.commit(files_under(work / "data"));
    }
    verify_data(work / "data", pins, strict_audio);

    emit("[stage] extract");
    emit("Vybalujeme a ověřujeme obrázky FLC");
    Checkpoint exported(work / "export-complete.json", sha_file(work / "data" / "GFX.GRP") + sha_file(tools / (WINDOWS ? "hl-tool.exe" : "hl-tool")));
    if (!exported.valid()) {
        run_tool(tools, {"assets", "export-frames", "--data", (work / "data").string(), "--out", (work / "frames").string(),
                         "--report", (work / "frames-report.json").string()}, work / "export-frames.log");
        run_tool(tools, {"assets", "export-font", "--data", (work / "data").string(), "--out", (work / "font").string(),
                         "--report", (work / "font-report.json").string()}, work / "export-font.log");
        auto files = files_under(work / "frames");
        auto font = files_under(work / "font");
        files.insert(files.end(), font.begin(), font.end());
        exported.commit(files);
    }

    emit("[stage] download");
    emit("Stahujeme šifrované překresby a ověřujeme SHA-256");
    auto& art = pins.at("art");
    auto art_zip = work / "art.zip";
    if (!req.art_archive.empty()) {
        if (sha_file(req.art_archive) != art.at("sha256").get<std::string>()) throw UserError("Vybraný archiv má jiný kontrolní součet.");
        if (!fs::exists(art_zip) || sha_file(art_zip) != art.at("sha256").get<std::string>())
            fs::copy_file(req.art_archive, art_zip, fs::copy_options::overwrite_existing);
        emit("Download 100%");
    } else {
        download(art.at("url"), art_zip, art.at("sha256"), emit);
    }

    emit("[stage] decrypt");
    emit("Odemykáme překresby klíči z tvého CD");
    decrypt_art(art_zip, work / "data" / "GFX.GRP", work / "art-decrypted", emit);

    emit("[stage] upscale");
    emit("Zvětšujeme obrázky (AnimeVideo v3); odhad času se upřesní po prvních snímcích");
    auto platform = WINDOWS ? "windows" : "macos";
    auto inference = work / "upscaler";
    remove_tree(inference);
    zip_extract_file(tools / "upscaler.zip", inference);
    Upscaler upscaler;
    upscaler.runner = inference / (WINDOWS ? "realesrgan-ncnn-vulkan.exe" : "realesrgan-ncnn-vulkan");
    upscaler.models = inference / "models";
    upscaler.runner_sha256 = pins.at("upscalers").at(platform).at("runner_sha256");
    upscaler.model = pins.at("model");
    if (sha_file(upscaler.runner) != upscaler.runner_sha256) throw UserError("Zvětšovač v průvodci nesedí s kontrolním součtem.");
    for (auto& m : pins.at("model").at("files"))
        if (sha_file(upscaler.models / fs::path(m.at("path").get<std::string>()).filename()) != m.at("sha256").get<std::string>())
            throw UserError("Model AnimeVideo v průvodci nesedí s kontrolním součtem.");
    if (!WINDOWS) fs::permissions(upscaler.runner, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec);
    auto resources = read_json(work / "frames" / "index.json").at("resources");
    std::sort(resources.begin(), resources.end(), [](const json& a, const json& b) { return a.at("name") < b.at("name"); });
    build_original({work / "frames", work / "cache", work / "packs" / "original", work / "evidence"}, upscaler, resources,
        [&](int done, int total, const std::string& name) { emit("[progress] upscale " + std::to_string(done) + " " + std::to_string(total) + " " + name); });
    emit("[detail] Barevné varianty pro jiné palety (menu, inventář, noc)");
    build_palette_variants({work / "frames", work / "cache", work / "packs" / "original", work / "evidence"}, upscaler, resources,
        pins.at("palette_variants"), [&](int done, int total, const std::string& name) {
            emit("[detail] Varianta " + std::to_string(done) + "/" + std::to_string(total) + ": " + name); });

    emit("[stage] remaster");
    build_remastered({work / "frames", work / "art-decrypted", work / "packs" / "original", work / "packs" / "remastered", work / "cache"}, upscaler,
        [&](int done, int total, const std::string& name) { emit("[progress] remaster " + std::to_string(done) + " " + std::to_string(total) + " " + name); });
    build_remastered_variants({work / "frames", work / "art-decrypted", work / "packs" / "original", work / "packs" / "remastered", work / "cache"},
        pins.at("palette_variants"));

    emit("[stage] app");
    auto target = assemble(tools, work, destination, req.cleanup, emit);
    if (req.cleanup) {
        emit("[detail] Mažu dočasné soubory");
        fs::current_path(previous);
        remove_tree(work);
    }
    emit("[stage] done");
    return target;
}
}
