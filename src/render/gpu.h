#pragma once
#include "render/scene_graph.h"
#include "render/palette.h"
#include "render/cache.h"
#include "render/viewport.h"
#include <SDL3/SDL.h>
#include <memory>
namespace hl {
struct AtlasDraw {FrameAsset asset;int x{},y{},z{};double scale_x{1},scale_y{1};};
class Renderer {
 ScalingMode scaling_{ScalingMode::Fill};
 struct Preload;
 std::unique_ptr<Preload> preload_;
 SDL_Renderer* renderer_{};SDL_Texture* canvas_{};SDL_Texture* processed_{};std::string gpu_failure_,adapter_;
 TextureCache<SDL_Texture> cache_{256*1024*1024};
 std::vector<std::shared_ptr<SDL_Texture>> in_flight_;
 Renderer();
 void release();Result<void> targets();
 Result<void> begin();Result<void> finish(PalettePostprocess);
 Result<std::shared_ptr<SDL_Texture>> texture(const FrameAsset&,const Pack&);
 Result<std::shared_ptr<SDL_Texture>> upload(const FrameAsset&,const AtlasImage&);
 Result<void> preload(const std::vector<RenderSurface>&,const FrameResolver&,std::string_view);
public:
 ~Renderer();
 static Result<std::unique_ptr<Renderer>> create(SDL_Window* window,std::string_view preference="auto");
 Result<void> use_software(std::string reason);
 void set_scaling(ScalingMode mode){scaling_=mode;}
 void scene(std::string_view);
 Result<bool> ready(const std::vector<RenderSurface>&,const FrameResolver&,GraphicsMode,std::string_view);
 const std::string& adapter()const{return adapter_;}
 Result<void> present(std::vector<RenderSurface> surfaces,PalettePostprocess effect={});
 Result<void> present(std::vector<RenderSurface> surfaces,const FrameResolver&,GraphicsMode,std::string palette,PalettePostprocess effect={});
 Result<void> present(std::vector<AtlasDraw> draws,const Pack&,PalettePostprocess effect={});
 Result<RenderCapture> readback()const;
 std::string backend()const;const std::string& gpu_failure()const{return gpu_failure_;}
 size_t cache_bytes()const{return cache_.bytes();}
 size_t texture_uploads()const{return cache_.insertions();}
 size_t texture_evictions()const{return cache_.evictions();}
 SDL_Renderer* sdl()const{return renderer_;}
};
}
