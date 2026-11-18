#include "render/gpu.h"
#include "render/viewport.h"
#include "render/renderer_policy.h"
#include "data/hash.h"
#include <algorithm>
#include <cstring>
#include <future>
#include <deque>
#include <set>
namespace hl {
namespace {
DataError error(){return {"","renderer",0,SDL_GetError()};}
std::string page_key(const FrameAsset& a){return std::to_string(int(a.mode))+"/"+a.atlas_hash+"/"+a.atlas.generic_string();}
}
struct Renderer::Preload {
 std::string scene;std::set<std::string> resources;std::map<std::string,FrameAsset> pages;
 std::deque<FrameAsset> queue;FrameAsset loading;std::future<Result<AtlasImage>> pending;
 std::vector<std::shared_ptr<SDL_Texture>> pins;
};
Renderer::Renderer():preload_(std::make_unique<Preload>()){}
void Renderer::scene(std::string_view id){if(preload_->scene!=id){preload_->scene=id;preload_->resources.clear();preload_->pages.clear();preload_->queue.clear();preload_->pins.clear();}}
Result<bool> Renderer::ready(const std::vector<RenderSurface>& surfaces,const FrameResolver& resolver,GraphicsMode mode,std::string_view palette){
 for(const auto& s:surfaces){
  if(!s.source.resource.empty()){auto a=resolver.resolve({s.source.resource,s.source.frame,s.opaque_base?"opaque_base":"overlay",s.source.palette.empty()?std::string(palette):s.source.palette},mode);if(!a.has_value())continue;if(!a.value().empty&&!cache_.find(page_key(a.value())))return false;}
  if(!s.glyphs.empty()){auto a=resolver.glyph(s.glyphs.front().index,mode);if(!a.has_value())return a.error();if(!cache_.find(page_key(a.value())))return false;}
 }
 return true;
}
Result<void> Renderer::preload(const std::vector<RenderSurface>& surfaces,const FrameResolver& resolver,std::string_view palette){
 auto& p=*preload_;p.pins.clear();
 auto live=[&](const FrameAsset& a){auto key=page_key(a);if(auto t=cache_.find(key)){p.pins.push_back(std::move(t));return;}if(p.pending.valid()&&page_key(p.loading)==key)return;if(std::none_of(p.queue.begin(),p.queue.end(),[&](const auto& queued){return page_key(queued)==key;}))p.queue.push_front(a);};
 auto add=[&](FrameAsset a){auto key=page_key(a);if(p.pages.emplace(key,a).second&&!cache_.find(key))p.queue.push_back(std::move(a));};
 for(auto mode:{GraphicsMode::Remastered,GraphicsMode::Original}){
  for(const auto& s:surfaces)if(!s.source.resource.empty()){
   ResourceFrameKey key{s.source.resource,s.source.frame,s.opaque_base?"opaque_base":"overlay",s.source.palette.empty()?std::string(palette):s.source.palette};
   auto id=std::to_string(int(mode))+"/"+key.resource+"/"+key.usage+"/"+key.palette;
   // Preloading is best effort: a frame this mode cannot resolve is reported by the draw path, not here.
   auto a=resolver.resolve(key,mode);if(!a.has_value())continue;if(!a.value().empty)live(a.value());
   if(p.resources.contains(id))continue;
   auto pages=a.value().owner->pages(key,mode);if(!pages.has_value())continue;for(auto& page:pages.value())add(std::move(page));p.resources.insert(std::move(id));
  }
  auto glyph=resolver.glyph(0,mode);if(glyph.has_value()){live(glyph.value());add(std::move(glyph.value()));}
 }
 if(p.pending.valid()&&p.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){
  auto image=p.pending.get();if(p.pages.contains(page_key(p.loading))){if(!image.has_value())return image.error();auto t=upload(p.loading,image.value());if(!t.has_value())return t.error();}
 }
 // SDL texture creation stays on the render thread; decoding and validation do not.
 if(!p.pending.valid())while(!p.queue.empty()){
  auto a=std::move(p.queue.front());p.queue.pop_front();if(cache_.find(page_key(a)))continue;p.loading=a;
  auto source=a.owner->atlas_source(a);p.pending=std::async(std::launch::async,[source=std::move(source),a=std::move(a)]{return source.load_atlas(a);});break;
 }
 return {};
}
void Renderer::release(){preload_->pins.clear();preload_->resources.clear();preload_->pages.clear();preload_->queue.clear();if(renderer_)SDL_FlushRenderer(renderer_);in_flight_.clear();cache_=TextureCache<SDL_Texture>(256*1024*1024);SDL_DestroyTexture(processed_);processed_=nullptr;SDL_DestroyTexture(canvas_);canvas_=nullptr;SDL_DestroyRenderer(renderer_);renderer_=nullptr;}
Renderer::~Renderer(){release();}
Result<void> Renderer::targets(){
 for(auto target:{&canvas_,&processed_}){*target=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,1920,1440);if(!*target)return error();if(!SDL_SetTextureBlendMode(*target,SDL_BLENDMODE_BLEND)||!SDL_SetTextureScaleMode(*target,SDL_SCALEMODE_LINEAR))return error();}
 SDL_SetRenderVSync(renderer_,0);return {};
}
Result<void> Renderer::use_software(std::string reason){
 auto window=SDL_GetRenderWindow(renderer_);release();gpu_failure_=std::move(reason);
 SDL_Log("Renderer fallback to software: %s",gpu_failure_.c_str());
 renderer_=SDL_CreateRenderer(window,"software");if(!renderer_)return error();return targets();
}
Result<std::unique_ptr<Renderer>> Renderer::create(SDL_Window* window,std::string_view preference) {
 if(!valid_renderer_preference(preference))return DataError{"","renderer",0,"invalid renderer preference"};
 auto out=std::unique_ptr<Renderer>(new Renderer);
 if(preference!="software")out->renderer_=SDL_CreateGPURenderer(nullptr,window);
 if(!out->renderer_){
  if(preference!="software"){out->gpu_failure_=SDL_GetError();SDL_Log("GPU creation failed, using software: %s",out->gpu_failure_.c_str());}
  out->renderer_=SDL_CreateRenderer(window,"software");
 }
 if(!out->renderer_)return error();
 if(auto gpu=SDL_GetGPURendererDevice(out->renderer_)){
  out->adapter_=SDL_GetStringProperty(SDL_GetGPUDeviceProperties(gpu),SDL_PROP_GPU_DEVICE_NAME_STRING,"");
#ifdef _WIN32
  if(preference=="auto"&&software_d3d12_adapter(SDL_GetGPUDeviceDriver(gpu),out->adapter_)){
   auto fallback=out->use_software("software D3D12 adapter: "+out->adapter_);if(!fallback.has_value())return fallback.error();
  }
#endif
 }
 if(!out->canvas_){auto targets=out->targets();if(!targets.has_value())return targets.error();}
 SDL_Log("Renderer selected: %s; adapter: %s; preference: %.*s",out->backend().c_str(),out->adapter_.c_str(),int(preference.size()),preference.data());return out;
}
std::string Renderer::backend()const {auto gpu=SDL_GetGPURendererDevice(renderer_);return gpu?std::string("gpu/")+SDL_GetGPUDeviceDriver(gpu):renderer_?SDL_GetRendererName(renderer_):"unavailable";}
Result<void> Renderer::begin(){
 // Flush deferred SDL draws before releasing cache pins from the previous frame.
 if(!SDL_FlushRenderer(renderer_))return error();in_flight_.clear();
 if(!SDL_SetRenderTarget(renderer_,canvas_)||!SDL_SetRenderDrawColor(renderer_,0,0,0,255)||!SDL_RenderClear(renderer_))return error();return {};
}
Result<std::shared_ptr<SDL_Texture>> Renderer::texture(const FrameAsset& a,const Pack& pack){
 // Cache the whole page; frame identity remains in the resolved asset key.
 auto key=page_key(a);if(auto hit=cache_.find(key))return hit;
 auto image=pack.load_atlas(a);if(!image.has_value())return image.error();
 return upload(a,image.value());
}
Result<std::shared_ptr<SDL_Texture>> Renderer::upload(const FrameAsset& a,const AtlasImage& image){
 auto key=page_key(a);if(auto hit=cache_.find(key))return hit;
 // SDL software static textures RLE-encode the entire atlas on its first draw.
 auto access=SDL_GetGPURendererDevice(renderer_)?SDL_TEXTUREACCESS_STATIC:SDL_TEXTUREACCESS_STREAMING;
 auto p=std::shared_ptr<SDL_Texture>(SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,access,image.width,image.height),SDL_DestroyTexture);if(!p)return error();
 if(!SDL_SetTextureBlendMode(p.get(),SDL_BLENDMODE_BLEND)||!SDL_SetTextureScaleMode(p.get(),SDL_SCALEMODE_NEAREST)||!SDL_UpdateTexture(p.get(),nullptr,image.pixels.data(),image.width*4))return error();
 return cache_.insert(key,image.pixels.size(),std::move(p));
}
Result<void> Renderer::present(std::vector<AtlasDraw> draws,const Pack& pack,PalettePostprocess effect){
 auto started=begin();if(!started.has_value())return started;
 std::stable_sort(draws.begin(),draws.end(),[](const auto& a,const auto& b){return a.z>b.z;});
 for(const auto& d:draws){if(d.asset.empty)continue;auto t=texture(d.asset,pack);if(!t.has_value())return t.error();auto p=place_asset(d.asset,d.x,d.y,d.scale_x,d.scale_y);SDL_FRect src{float(d.asset.u),float(d.asset.v),float(d.asset.width),float(d.asset.height)},dst{float(p.x),float(p.y),float(p.width),float(p.height)};if(!SDL_RenderTexture(renderer_,t.value().get(),&src,&dst))return error();in_flight_.push_back(std::move(t.value()));}
 return finish(effect);
}
Result<void> Renderer::present(std::vector<RenderSurface> surfaces,const FrameResolver& resolver,GraphicsMode mode,std::string palette,PalettePostprocess effect){
 auto started=begin();if(!started.has_value())return started;
 auto warmed=preload(surfaces,resolver,palette);if(!warmed.has_value())return warmed;
 std::stable_sort(surfaces.begin(),surfaces.end(),[](const auto& a,const auto& b){return a.z>b.z;});
 for(const auto& s:surfaces){
  if(!s.source.resource.empty()){
   auto asset=resolver.resolve({s.source.resource,s.source.frame,s.opaque_base?"opaque_base":"overlay",s.source.palette.empty()?palette:s.source.palette},mode);if(!asset.has_value())return asset.error();auto& a=asset.value();if(a.empty)continue;
   if(!a.owner||s.source.width<=0||s.source.height<=0)return DataError{"","renderer",0,"invalid source projection"};
   auto t=texture(a,*a.owner);if(!t.has_value())return t.error();
   int left=std::max(a.x,s.source.x*3),top=std::max(a.y,s.source.y*3),right=std::min(a.x+a.width,(s.source.x+s.source.width)*3),bottom=std::min(a.y+a.height,(s.source.y+s.source.height)*3);if(left>=right||top>=bottom)continue;
   double sx=double(s.width)/s.source.width,sy=double(s.height)/s.source.height;
   SDL_FRect source{float(a.u+left-a.x),float(a.v+top-a.y),float(right-left),float(bottom-top)},destination{float(s.x*3+(left-s.source.x*3)*sx),float(s.y*3+(top-s.source.y*3)*sy),float((right-left)*sx),float((bottom-top)*sy)};
   auto extent=surface_clip(s);SDL_Rect clip{0,0,extent[0],extent[1]};if(!SDL_SetRenderClipRect(renderer_,&clip)||!SDL_RenderTexture(renderer_,t.value().get(),&source,&destination)||!SDL_SetRenderClipRect(renderer_,nullptr))return error();in_flight_.push_back(std::move(t.value()));
  }else{
   if(s.width<=0||s.height<=0||uint64_t(s.width)*s.height*4!=s.rgba.size())return DataError{"","renderer",0,"invalid overlay surface"};
   auto key="overlay/"+std::to_string(int(mode))+"/"+sha256(std::as_bytes(std::span(s.rgba)))+"/"+std::to_string(s.width)+"/"+std::to_string(s.height);auto t=cache_.find(key);
   if(!t){t=std::shared_ptr<SDL_Texture>(SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,s.width,s.height),SDL_DestroyTexture);if(!t)return error();if(!SDL_SetTextureBlendMode(t.get(),SDL_BLENDMODE_BLEND)||!SDL_SetTextureScaleMode(t.get(),SDL_SCALEMODE_NEAREST)||!SDL_UpdateTexture(t.get(),nullptr,s.rgba.data(),s.width*4))return error();auto inserted=cache_.insert(key,s.rgba.size(),t);if(!inserted.has_value())return inserted.error();}
   SDL_FRect destination{float(s.x*3),float(s.y*3),float(s.width*3),float(s.height*3)};if(!SDL_RenderTexture(renderer_,t.get(),nullptr,&destination))return error();in_flight_.push_back(std::move(t));
  }
  if(s.dos_text_clip){auto extent=surface_clip(s);SDL_Rect clip{0,0,extent[0],extent[1]};if(!SDL_SetRenderClipRect(renderer_,&clip))return error();}
  if(!s.glyphs.empty()) {
   auto first=resolver.glyph(s.glyphs.front().index,mode);if(!first.has_value())return first.error();auto t=texture(first.value(),*first.value().owner);if(!t.has_value())return t.error();
   std::vector<SDL_Vertex> vertices;std::vector<int> indices;vertices.reserve(s.glyphs.size()*4);indices.reserve(s.glyphs.size()*6);
   SDL_FColor color{float(s.text_color[0])/255,float(s.text_color[1])/255,float(s.text_color[2])/255,1};
   for(const auto& g:s.glyphs){auto asset=resolver.glyph(g.index,mode);if(!asset.has_value())return asset.error();const auto& a=asset.value();
    float x=float(g.x*3),y=float(g.y*3),u=float(a.u)/a.atlas_width,v=float(a.v)/a.atlas_height,r=float(a.u+24)/a.atlas_width,b=float(a.v+48)/a.atlas_height;int base=int(vertices.size());
    vertices.insert(vertices.end(),{{{x,y},color,{u,v}},{{x+24,y},color,{r,v}},{{x+24,y+48},color,{r,b}},{{x,y+48},color,{u,b}}});indices.insert(indices.end(),{base,base+1,base+2,base,base+2,base+3});
   }
   if(!SDL_RenderGeometry(renderer_,t.value().get(),vertices.data(),int(vertices.size()),indices.data(),int(indices.size())))return error();in_flight_.push_back(std::move(t.value()));
  }
  if(s.dos_text_clip&&!SDL_SetRenderClipRect(renderer_,nullptr))return error();
 }
 return finish(effect);
}
Result<void> Renderer::present(std::vector<RenderSurface> surfaces,PalettePostprocess effect) {
 auto started=begin();if(!started.has_value())return started;
 std::stable_sort(surfaces.begin(),surfaces.end(),[](const auto& a,const auto& b){return a.z>b.z;});
 for(const auto& s:surfaces) {
  if(s.width<=0||s.height<=0||uint64_t(s.width)*s.height*4!=s.rgba.size())return DataError{"","renderer",0,"invalid render surface"};
  auto key="surface/"+sha256(std::as_bytes(std::span(s.rgba)))+"/"+std::to_string(s.width)+"/"+std::to_string(s.height);auto t=cache_.find(key);
  if(!t){t=std::shared_ptr<SDL_Texture>(SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,s.width,s.height),SDL_DestroyTexture);if(!t)return error();if(!SDL_SetTextureBlendMode(t.get(),SDL_BLENDMODE_BLEND)||!SDL_SetTextureScaleMode(t.get(),SDL_SCALEMODE_NEAREST)||!SDL_UpdateTexture(t.get(),nullptr,s.rgba.data(),s.width*4))return error();auto inserted=cache_.insert(key,s.rgba.size(),std::move(t));if(!inserted.has_value())return inserted.error();t=std::move(inserted.value());}
  SDL_FRect destination{float(s.x*3),float(s.y*3),float(s.width*3),float(s.height*3)};if(!SDL_RenderTexture(renderer_,t.get(),nullptr,&destination))return error();in_flight_.push_back(std::move(t));
 }
 return finish(effect);
}
Result<void> Renderer::finish(PalettePostprocess effect){
 if(effect.gain<0||effect.bias<0||effect.gain+effect.bias>1.00001f)return DataError{"","renderer",0,"invalid postprocess gain/bias"};
 // The pinned SDL Metal/DXIL texture shader applies the post-composition gain.
 // Blending over the bias colour implements fades to white without repainting layers.
 auto value=uint8_t(std::clamp(effect.bias*255.0f,0.0f,255.0f));
 if(!SDL_SetRenderTarget(renderer_,processed_)||!SDL_SetRenderDrawColor(renderer_,value,value,value,255)||!SDL_RenderClear(renderer_)||!SDL_SetTextureColorModFloat(canvas_,effect.gain,effect.gain,effect.gain)||!SDL_RenderTexture(renderer_,canvas_,nullptr,nullptr))return error();
 if(effect.bias>0){SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_ADD);SDL_SetRenderDrawColor(renderer_,value,value,value,255);SDL_RenderFillRect(renderer_,nullptr);SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);}
 int w{},h{};if(!SDL_SetRenderTarget(renderer_,nullptr)||!SDL_GetRenderOutputSize(renderer_,&w,&h)||!SDL_SetRenderDrawColor(renderer_,0,0,0,255)||!SDL_RenderClear(renderer_))return error();auto v=Viewport::fit(w,h,scaling_);SDL_FRect dst{float(v.x),float(v.y),float(v.width),float(v.height)};if(!SDL_SetTextureScaleMode(processed_,scaling_==ScalingMode::Whole&&!v.fractional?SDL_SCALEMODE_NEAREST:SDL_SCALEMODE_LINEAR)||!SDL_RenderTexture(renderer_,processed_,nullptr,&dst))return error();return {};
}
Result<RenderCapture> Renderer::readback()const {
 auto prior=SDL_GetRenderTarget(renderer_);if(!SDL_SetRenderTarget(renderer_,processed_))return error();auto surface=SDL_RenderReadPixels(renderer_,nullptr);SDL_SetRenderTarget(renderer_,prior);auto rgba=surface?SDL_ConvertSurface(surface,SDL_PIXELFORMAT_RGBA32):nullptr;
 if(!rgba){SDL_DestroySurface(surface);return error();}
 std::vector<uint8_t> out(size_t(rgba->w)*rgba->h*4);for(int y=0;y<rgba->h;++y)std::memcpy(out.data()+size_t(y)*rgba->w*4,static_cast<uint8_t*>(rgba->pixels)+size_t(y)*rgba->pitch,size_t(rgba->w)*4);
 int width=rgba->w,height=rgba->h;SDL_DestroySurface(rgba);SDL_DestroySurface(surface);RenderCapture capture{width,height,std::move(out)};auto valid=validate_capture(capture);if(!valid.has_value())return valid.error();return capture;
}
}
