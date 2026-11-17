#include "platform/app.h"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
namespace hl {
int run_app(const AppOptions& options) {
    if(!options.data_path.empty())return run_native_app(options);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    SDL_Window* window = SDL_CreateWindow("Horke Leto M0", 640, 480, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        std::cerr << SDL_GetError() << '\n';
        SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    std::uint32_t frames = 0;
    bool quit = false, failed = false;
    while (!quit && (!options.smoke_test || frames < options.frames)) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) quit = true;
        if (quit) break;
        if (!SDL_SetRenderDrawColor(renderer, 24, 32, 48, 255) ||
            !SDL_RenderClear(renderer)) { failed = true; break; }
        if (!options.capture_path.empty() && frames + 1 == options.frames) {
            SDL_Surface* frame = SDL_RenderReadPixels(renderer, nullptr);
            SDL_Surface* rgba = frame ? SDL_ConvertSurface(frame, SDL_PIXELFORMAT_RGBA32) : nullptr;
            if (!rgba || !stbi_write_png(options.capture_path.c_str(), rgba->w, rgba->h,
                                         4, rgba->pixels, rgba->pitch)) failed = true;
            SDL_DestroySurface(rgba); SDL_DestroySurface(frame);
            if (failed) { std::cerr << "frame readback/write failed: " << SDL_GetError() << '\n'; break; }
        }
        if (!SDL_RenderPresent(renderer)) { failed = true; break; }
        ++frames;
        SDL_Delay(options.smoke_test ? 1000 : 16);
    }
    const std::string backend = SDL_GetRendererName(renderer);
    const std::string video_driver = SDL_GetCurrentVideoDriver();
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    const bool complete = !failed && (!options.smoke_test || frames == options.frames);
    if (!options.report_path.empty()) {
        nlohmann::json report{{"schema_version",1},{"platform",SDL_GetPlatform()},
#if defined(__aarch64__) || defined(_M_ARM64)
            {"architecture","arm64"},
#elif defined(__x86_64__) || defined(_M_X64)
            {"architecture","x64"},
#else
            {"architecture","unknown"},
#endif
            {"sdl_version",SDL_GetVersion()},{"backend",backend},{"video_driver",video_driver},
            {"frames_presented",frames},{"smoke_test",options.smoke_test},
            {"capture",options.capture_path},{"capture_method","SDL_RenderReadPixels before present"},
            {"resources_released",true},{"exit_code",complete ? 0 : 1}};
        std::ofstream output(options.report_path);
        output << report.dump(2) << '\n';
        if (!output) { std::cerr << "report write failed\n"; return 1; }
    }
    return complete ? 0 : 1;
}
}
