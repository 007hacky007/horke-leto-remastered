#include "viewer.h"
#include "assets.h"
#include "image_io.h"
#include "data/grp.h"
#include "data/font.h"
#include "data/hash.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
#include <map>
#include <charconv>
namespace {
hl::Result<void> capture(SDL_Renderer* renderer,const std::filesystem::path& path){auto* surface=SDL_RenderReadPixels(renderer,nullptr);if(!surface)return hl::DataError{"","viewer",0,SDL_GetError()};auto* rgba=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(surface);if(!rgba)return hl::DataError{"","viewer",0,SDL_GetError()};std::vector<uint8_t> pixels;pixels.reserve(size_t(rgba->w)*rgba->h*4);for(int y=0;y<rgba->h;++y){auto* row=static_cast<const uint8_t*>(rgba->pixels)+y*rgba->pitch;pixels.insert(pixels.end(),row,row+rgba->w*4);}auto result=hl::write_png(path,rgba->w,rgba->h,4,pixels);SDL_DestroySurface(rgba);return result;}
}
int viewer_command(int argc,char** argv){
 std::map<std::string,std::string> args;for(int i=1;i<argc;++i){std::string key=argv[i];if(key=="--sweep"||key=="--opaque"||key=="--mask"||key=="--palette"||key=="--crop"||key=="--font"){args[key]="true";continue;}if(i+1>=argc||!args.emplace(key,argv[i+1]).second)return 2;++i;}
 if(!args.contains("--data"))return 2;auto archive=hl::GrpArchive::open(std::filesystem::path(args["--data"])/"GFX.GRP");if(!archive.has_value()){std::cerr<<archive.error().reason<<'\n';return 1;}
 std::vector<hl::ViewerResource> catalog;size_t total=0;
 for(auto& e:archive.value().entries())if(hl::ascii_key(e.name).ends_with(".flc")){auto b=archive.value().read(e.name);if(!b.has_value())return 1;auto d=hl::FlcDecoder::open(b.value(),e.name);if(!d.has_value()){std::cerr<<e.name<<":"<<d.error().byte_offset<<": "<<d.error().reason<<'\n';return 1;}auto canvas=hl::validate_viewer_canvas(d.value().width(),d.value().height(),e.name);if(!canvas.has_value()){std::cerr<<e.name<<":8: "<<canvas.error().reason<<'\n';return 1;}catalog.push_back({e.name,d.value().declared_frames()});total+=d.value().declared_frames();}
 auto created=hl::ViewerModel::create(catalog);if(!created.has_value())return 1;auto model=std::move(created.value());uint32_t frame=0;size_t limit=0;
 auto number=[](const std::string& s,uint64_t& out){auto r=std::from_chars(s.data(),s.data()+s.size(),out);return r.ec==std::errc{}&&r.ptr==s.data()+s.size();};uint64_t value;
 if(args.contains("--frame")){if(!number(args["--frame"],value)||value>UINT32_MAX)return 2;frame=uint32_t(value);}if(args.contains("--frames")){if(!number(args["--frames"],value)||!value||value>SIZE_MAX)return 2;limit=size_t(value);}
 if(args.contains("--resource")){auto r=model.seek(args["--resource"],frame);if(!r.has_value()){std::cerr<<r.error().reason<<'\n';return 1;}}else if(frame)return 2;
 bool sweep=args.contains("--sweep"),playing=false,opaque=args.contains("--opaque"),mask=args.contains("--mask"),palette=args.contains("--palette"),crop=args.contains("--crop"),font=args.contains("--font");if(sweep){model.seek(catalog[0].name,0);limit=total;}
 if(!SDL_Init(SDL_INIT_VIDEO)){std::cerr<<SDL_GetError()<<'\n';return 1;}SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;
 if(!SDL_CreateWindowAndRenderer("Horke Leto resources",960,720,SDL_WINDOW_RESIZABLE,&window,&renderer)){std::cerr<<SDL_GetError()<<'\n';SDL_Quit();return 1;}SDL_SetRenderLogicalPresentation(renderer,640,480,SDL_LOGICAL_PRESENTATION_LETTERBOX);SDL_SetRenderVSync(renderer,0);
 auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,640,480);if(!texture){SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 1;}
 nlohmann::json report{{"schema_version",1},{"platform",SDL_GetPlatform()},{"renderer",SDL_GetRendererName(renderer)},{"capture_method","SDL_RenderReadPixels before present"},{"cadence","PIT minimum: 49700/1193182 seconds per engine update; original work can slow it"},{"visited",nlohmann::json::array()}};
 std::optional<hl::FlcDecoder> decoder;std::optional<hl::IndexedFrame> shown;std::string loaded;uint32_t decoded_index=UINT32_MAX;std::vector<uint8_t> rgba(640*480*4);std::optional<hl::BitmapFont> original_font;
 bool running=true,dirty=true,failed=false;size_t presented=0;hl::FrameClock clock;uint64_t start=SDL_GetTicksNS(),deadline=start;
 auto error=[&](const hl::DataError& e){std::cerr<<e.resource<<":"<<e.byte_offset<<": "<<e.reason<<'\n';report["error"]={{"resource",e.resource},{"offset",e.byte_offset},{"reason",e.reason}};failed=true;running=false;};
 while(running){
  SDL_Event event;while(SDL_PollEvent(&event)){if(event.type==SDL_EVENT_QUIT)running=false;if(event.type==SDL_EVENT_KEY_DOWN){auto key=event.key.key;
   if(key==SDLK_ESCAPE)running=false;else if(key==SDLK_RIGHT){model.advance();dirty=true;}else if(key==SDLK_LEFT){model.previous();dirty=true;}else if(key==SDLK_PAGEDOWN){model.resource(1);dirty=true;}else if(key==SDLK_PAGEUP){model.resource(-1);dirty=true;}else if(key==SDLK_SPACE){playing=!playing;start=SDL_GetTicksNS();clock=hl::FrameClock{};deadline=start;}
   else if(key==SDLK_M){mask=!mask;dirty=true;}else if(key==SDLK_P){palette=!palette;dirty=true;}else if(key==SDLK_C){crop=!crop;dirty=true;}else if(key==SDLK_O){opaque=!opaque;dirty=true;}else if(key==SDLK_F){font=!font;dirty=true;}}
   if(event.type==SDL_EVENT_WINDOW_EXPOSED||event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)dirty=true;
  }
  if(!running)break;
  if(playing&&!dirty&&SDL_GetTicksNS()>=deadline){if(model.frame()+1==model.current().frames)model.seek(model.current().name,0);else model.advance();dirty=true;}
  if(!dirty){SDL_Delay(1);continue;}
  if(loaded!=model.current().name){auto b=archive.value().read(model.current().name);if(!b.has_value()){error(b.error());break;}auto d=hl::FlcDecoder::open(b.value(),model.current().name);if(!d.has_value()){error(d.error());break;}decoder=std::move(d.value());loaded=model.current().name;decoded_index=UINT32_MAX;}
  if(!shown||decoded_index!=model.frame()){
   if(decoded_index!=UINT32_MAX&&model.frame()==decoded_index+1){auto f=decoder->next();if(!f.has_value()){error(f.error());break;}if(!f.value()){error({"",loaded,model.frame(),"missing visible frame"});break;}shown=std::move(*f.value());}
   else{auto f=decoder->seek(model.frame());if(!f.has_value()){error(f.error());break;}shown=std::move(f.value());}decoded_index=model.frame();
  }
  auto& f=*shown;for(size_t i=0;i<f.indices.size();++i){auto index=f.indices[i];auto color=f.palette[index];uint8_t background=((i%640/16+i/640/16)%2)?48:96;if(mask)color=index?std::array<uint8_t,3>{255,255,255}:std::array<uint8_t,3>{0,0,0};else if(!opaque&&!index)color={background,background,background};for(unsigned c=0;c<3;++c)rgba[i*4+c]=color[c];rgba[i*4+3]=255;}
  if(palette)for(size_t i=0;i<256;++i)for(size_t y=0;y<8;++y)for(size_t x=0;x<2;++x){auto p=((460+y)*640+i*2+x)*4;for(unsigned c=0;c<3;++c)rgba[p+c]=f.palette[i][c];}
  if(font){if(!original_font){auto b=hl::read_file(std::filesystem::path(args["--data"])/"HL.EXE");if(!b.has_value()){error(b.error());break;}auto loaded_font=hl::load_original_font({b.value(),252284});if(!loaded_font.has_value()){error(loaded_font.error());break;}original_font=std::move(loaded_font.value());}
   for(unsigned i=0;i<256;++i)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<8;++x){auto p=((100+i/16*16+y)*640+256+i%16*8+x)*4;uint8_t v=(original_font->glyphs[i].rows[y]&(128>>x))?255:0;for(unsigned c=0;c<3;++c)rgba[p+c]=v;}
  }
  if(!SDL_UpdateTexture(texture,nullptr,rgba.data(),640*4)||!SDL_RenderClear(renderer)||!SDL_RenderTexture(renderer,texture,nullptr,nullptr)){error({"",loaded,model.frame(),SDL_GetError()});break;}
  if(crop){auto box=hl::crop_frame(f,{0,"analysis/evidence/transparency.json"});if(!box.has_value()){error(box.error());break;}if(!box.value().empty){auto& b=box.value();SDL_FRect rect{float(b.x),float(b.y),float(b.width),float(b.height)};SDL_SetRenderDrawColor(renderer,255,0,255,255);SDL_RenderRect(renderer,&rect);}}
  auto title="Horke Leto: "+loaded+" frame "+std::to_string(model.frame())+" | arrows/PgUp/PgDn Space M P C O F";SDL_SetWindowTitle(window,title.c_str());
  if(presented==0&&args.contains("--capture")){auto c=capture(renderer,args["--capture"]);if(!c.has_value()){error(c.error());break;}}
  if(!SDL_RenderPresent(renderer)){error({"",loaded,model.frame(),SDL_GetError()});break;}
  report["visited"].push_back({{"resource",loaded},{"frame",model.frame()},{"indices_sha256",hl::sha256(std::as_bytes(std::span(f.indices)))}});++presented;dirty=false;
  if(limit&&presented>=limit)running=false;else if(sweep){model.advance();dirty=true;}
  // shortcut: redraws also extend the deadline; separate redraw scheduling when tuning interactive playback.
  if(playing){auto advanced=clock.advance({f.effective_delay.numerator*49700,1193182});if(!advanced.has_value()){error(advanced.error());break;}deadline=start+clock.elapsed_nanoseconds();}
 }
 report["frames_presented"]=presented;report["catalog_resources"]=catalog.size();report["catalog_frames"]=total;report["passed"]=!failed&&(!sweep||presented==total);if(args.contains("--report")){std::ofstream out(args["--report"]);out<<report.dump(2)<<'\n';if(!out)failed=true;}
 SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return failed?1:0;
}
