#include "preflight.h"
#include "disc.h"
#include "pipeline.h"
#include <cstdio>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace hb {
constexpr double NEED_GB = 40, FINAL_GB = 9;

static Check check_disc(const fs::path& image, const std::string& drive, const json& pins) {
    Check c{"disc", "Správné vydání hry"};
    if (!drive.empty()) {
        fs::path root = fs::path(drive + "\\HL");
        if (!fs::exists(root)) root = fs::path(drive + "\\");
        for (auto& [name, expected] : pins.at("files").items()) {
            if (!fs::is_regular_file(root / name) || sha_file(root / name) != expected.get<std::string>()) {
                c.state = Check::Fail;
                c.text = "Soubor " + name + " na CD v mechanice " + drive + " chybí nebo nesedí.";
                c.hint = "Vlož CD Horkého léta, případně ho očisti.";
                return c;
            }
        }
        c.text = "CD v mechanice " + drive + ": herní soubory sedí. Hudbu přečteme při sestavení.";
        return c;
    }
    if (image.empty()) { c.state = Check::Fail; c.text = "Nevybral jsi žádnou zálohu."; c.hint = "Vrať se a vyber obraz CD."; return c; }
    try {
        auto kind = identify_disc(image, disc_pins(pins), [](const std::string&) {});
        c.text = format_name(kind) + ": hra i hudební stopy sedí s podporovaným vydáním.";
    } catch (const UserError& error) {
        c.state = Check::Fail; c.text = error.what(); c.hint = "Vyber úplný obraz CD Horkého léta.";
    }
    return c;
}

static double folder_gb(const fs::path& path) {
    double bytes = 0;
    std::error_code ec;
    if (fs::is_directory(path, ec))
        for (auto& e : fs::recursive_directory_iterator(path, ec)) if (e.is_regular_file(ec)) bytes += double(e.file_size(ec));
    return bytes / (1ull << 30);
}

static Check check_space(const fs::path& output) {
    Check c{"space", "Místo na disku"};
    auto probe = output;
    while (!probe.empty() && !fs::exists(probe)) probe = probe.parent_path();
    double free = double(available_bytes(probe)) / (1ull << 30);
    double done = folder_gb(output / ".builder");
    double need = std::max(FINAL_GB, NEED_GB - done);
    char text[200];
    std::snprintf(text, sizeof text, "Volných %.0f GB, potřebujeme asi %.0f GB. Po úklidu zůstane hra, asi %.0f GB.%s",
                  free, need, FINAL_GB, done > 1 ? " Navážeme na rozdělané sestavení." : "");
    c.text = text;
    if (free < need) { c.state = Check::Fail; c.hint = "Vyber jiný disk nebo uvolni místo."; c.fix = "output"; }
    return c;
}

static Check check_write(const fs::path& output, const fs::path& image) {
    Check c{"write", "Cílová složka", Check::Ok, output.string()};
    auto fail = [&](const char* text, const char* hint) { c.state = Check::Fail; c.text = text; c.hint = hint; c.fix = "output"; return c; };
    if (fs::exists(output / app_name())) return fail("V cílové složce už hotová hra je.", "Vyber jinou, prázdnou složku.");
    if (!image.empty()) {
        auto source = fs::absolute(image).parent_path(), target = fs::absolute(output);
        if (target == source || std::mismatch(source.begin(), source.end(), target.begin(), target.end()).first == source.end())
            return fail("Cíl nesmí být ve složce se zálohou.", "Vyber jinou složku.");
    }
    std::error_code ec;
    fs::create_directories(output, ec);
    auto probe = output / ".write-test";
    { std::ofstream out(probe); out << "ok"; if (!out) return fail("Do složky nejde zapisovat.", "Vyber složku, kam smíš zapisovat."); }
    fs::remove(probe, ec);
    return c;
}

static Check check_gpu() {
    Check c{"gpu", "Grafika pro zvětšování"};
#ifdef _WIN32
    HMODULE vulkan = LoadLibraryW(L"vulkan-1.dll");
    if (!vulkan) { c.state = Check::Fail; c.text = "Chybí Vulkan, zvětšování bez něj nepojede."; c.hint = "Nainstaluj aktuální ovladač grafické karty."; }
    else { FreeLibrary(vulkan); c.text = "Vulkan je k dispozici."; }
#else
    c.text = "Metal (Apple Silicon).";
#endif
    return c;
}

static Check check_network(const json& pins, const fs::path& archive) {
    Check c{"network", "Výtvarný balíček"};
    if (!archive.empty()) { c.text = "Použijeme místní archiv " + archive.filename().string() + "."; return c; }
    std::string version = pins.at("art").at("version");
    if (!url_reachable(pins.at("art").at("url"))) {
        c.state = Check::Fail; c.text = "Výtvarný balíček " + version + " teď nejde stáhnout.";
        c.hint = "Zkontroluj připojení, nebo vyber stažený archiv."; c.fix = "archive";
    } else c.text = "Balíček " + version + " je dostupný.";
    return c;
}

static Check check_art(const json& pins, const fs::path& archive) {
    Check c{"art", "Kontrolní součet balíčku"};
    if (archive.empty()) { c.text = "SHA-256 ověříme hned po stažení."; return c; }
    if (sha_file(archive) != pins.at("art").at("sha256").get<std::string>()) {
        c.state = Check::Fail; c.text = "Archiv má jiný kontrolní součet.";
        c.hint = "Stáhni přesně verzi " + pins.at("art").at("version").get<std::string>() + "."; c.fix = "archive";
    } else c.text = "Kontrolní součet archivu sedí.";
    return c;
}

void run_preflight(const fs::path& image, const std::string& drive, const fs::path& output, const fs::path& archive,
                   const std::function<void(const Check&)>& row) {
    auto pins = builder_pins();
    auto guard = [&](const char* key, const char* title, const std::function<Check()>& check) {
        try { row(check()); }
        catch (const std::exception& error) { Check c{key, title, Check::Fail, error.what()}; row(c); }
    };
    guard("disc", "Správné vydání hry", [&] { return check_disc(image, drive, pins); });
    guard("space", "Místo na disku", [&] { return check_space(output); });
    guard("write", "Cílová složka", [&] { return check_write(output, image); });
    guard("gpu", "Grafika pro zvětšování", check_gpu);
    guard("network", "Výtvarný balíček", [&] { return check_network(pins, archive); });
    guard("art", "Kontrolní součet balíčku", [&] { return check_art(pins, archive); });
#ifdef _WIN32
    row({"time", "Odhad času", Check::Ok, "Asi 1 až 2 hodiny podle grafické karty. Přerušené sestavení umí pokračovat."});
#else
    row({"time", "Odhad času", Check::Ok, "Asi 75 minut na M1 Pro. Přerušené sestavení umí pokračovat."});
#endif
}
}
