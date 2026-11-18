// Horke Leto Remastered builder. Without arguments it opens the window; --cli runs headless.
#include "pipeline.h"
#include "update.h"
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

int run_window(int argc, char** argv);
std::string replaced_version;  // set when this builder was started by an older one updating itself

int main(int argc, char** argv) {
#ifdef _WIN32
    // A GUI-subsystem exe has no console; borrow the caller's for --cli and --self-check.
    if (argc > 1 && AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
#endif
    bool cli = false;
    hb::BuildRequest request;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--self-check") {
            try { std::printf("OK %s\n", hb::self_check().c_str()); return 0; }
            catch (const std::exception& error) { std::fprintf(stderr, "FAILED: %s\n", error.what()); return 1; }
        }
        if (a == "--cli") cli = true;
        else if (a == "--image") request.image = hb::fs::u8path(next());
        else if (a == "--drive") request.drive = next();
        else if (a == "--output") request.output = hb::fs::u8path(next());
        else if (a == "--art-archive") request.art_archive = hb::fs::u8path(next());
        else if (a == "--keep-temporary") request.cleanup = false;
    }
    if (argc > 1 && std::string(argv[1]) == "--version") { std::printf("%s\n", hb::builder_version()); return 0; }
    if (!cli) {
        replaced_version = hb::finish_update(argc, argv);
        return run_window(argc, argv);
    }
    try {
        auto result = hb::build_game(request, [](const std::string& line) { std::printf("%s\n", line.c_str()); std::fflush(stdout); });
        std::printf("OK %s\n", result.string().c_str());
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHYBA: %s\n", error.what());
        return 1;
    }
}
