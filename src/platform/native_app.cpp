#include "platform/app.h"
#include <cstdio>
#include "script/intro_runtime.h"
#include "debug/replay.h"
#include "platform/runtime_mouse.h"
#include "platform/window.h"
#include "platform/player_ui.h"
#include "render/gpu.h"
#include "render/renderer_policy.h"
#include "data/hash.h"
#include "render/viewport.h"
#include "render/graphics_mode.h"
#include "render/active_areas.h"
#include "data/font.h"
#include "data/grp.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include "stb_image_write.h"
namespace hl {
namespace {
Result<void> write_json(const std::filesystem::path& file,const nlohmann::json& value) {std::ofstream out(file);out<<value.dump(2)<<'\n';if(!out)return DataError{"",file.filename().string(),0,"cannot write native evidence"};return {};}
}
std::vector<RenderSurface> options_surfaces(const std::vector<OptionRow>& rows,const BitmapFont& font,bool packed){
 std::vector<RenderSurface> out;
 auto rectangle=[&](int x,int y,int z,int width,int height,std::array<uint8_t,4> color){RenderSurface surface{x,y,z,width,height,{}};surface.rgba.resize(size_t(width)*height*4);for(size_t i=0;i<surface.rgba.size();i+=4)std::copy(color.begin(),color.end(),surface.rgba.begin()+i);out.push_back(std::move(surface));};
 rectangle(56,96,-200,528,304,{20,24,32,245});
 for(const auto& row:rows){
  if(row.selected)rectangle(row.x,row.y,-201,row.width,24,{55,66,80,255});
  std::vector<uint8_t> bytes(row.label.begin(),row.label.end());auto mask=render_font(font,bytes);if(!mask.has_value())continue;
  std::array<uint8_t,3> color=row.enabled?std::array<uint8_t,3>{240,235,210}:std::array<uint8_t,3>{100,104,112};
  RenderSurface text{row.x+4,row.y+4,-202,int(bytes.size())*8,16,{}};
  for(auto bit:mask.value())for(auto v:{color[0],color[1],color[2],packed?uint8_t(0):bit})text.rgba.push_back(v);
  if(packed){text.text_color=color;for(size_t i=0;i<bytes.size();++i)text.glyphs.push_back({bytes[i],text.x+int(i)*8,text.y});}
  out.push_back(std::move(text));
 }
 return out;
}
int run_native_app(const AppOptions& options) {
 if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)){std::cerr<<SDL_GetError()<<'\n';return 1;}
 std::filesystem::path pref_path;
 if(!options.save_directory.empty()){pref_path=std::filesystem::u8path(options.save_directory);std::filesystem::create_directories(pref_path);}
 else {char* pref=SDL_GetPrefPath("hl-port","HorkeLeto");if(!pref){std::cerr<<SDL_GetError()<<'\n';SDL_Quit();return 1;}pref_path=pref;SDL_free(pref);}
 auto settings_file=options.graphics_settings.empty()?pref_path/"graphics.json":std::filesystem::u8path(options.graphics_settings);GraphicsSettings settings;
 if(std::filesystem::exists(settings_file)){auto loaded=load_graphics_settings(settings_file);if(!loaded.has_value()){std::cerr<<loaded.error().reason<<'\n';SDL_Quit();return 1;}settings=loaded.value();}
 auto preference=options.renderer.empty()?settings.renderer:options.renderer;
 auto window=SDL_CreateWindow("Horke Leto - O nastaveni | G grafika",640,480,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|SDL_WINDOW_HIDDEN);
 if(window){auto configured=configure_window(window,settings);if(!configured.has_value()){std::cerr<<configured.error().reason<<'\n';SDL_DestroyWindow(window);SDL_Quit();return 1;}}
 if(window&&!SDL_ShowWindow(window)){std::cerr<<SDL_GetError()<<'\n';SDL_DestroyWindow(window);SDL_Quit();return 1;}
 auto renderer=Renderer::create(window,preference);
 if(!window||!renderer.has_value()){std::cerr<<SDL_GetError()<<'\n';SDL_DestroyWindow(window);SDL_Quit();return 1;}
 SDL_HideCursor();SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};auto stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
 if(!stream||!SDL_ResumeAudioStreamDevice(stream)){std::cerr<<SDL_GetError()<<'\n';SDL_DestroyAudioStream(stream);renderer.value().reset();SDL_DestroyWindow(window);SDL_Quit();return 1;}
 auto runtime=options.load_save.empty()?IntroRuntime::open(std::filesystem::u8path(options.data_path)):IntroRuntime::restore(std::filesystem::u8path(options.data_path),std::filesystem::u8path(options.load_save));
 if(!runtime.has_value()){std::cerr<<runtime.error().reason<<'\n';SDL_DestroyAudioStream(stream);renderer.value().reset();SDL_DestroyWindow(window);SDL_Quit();return 1;}
 auto& game=*runtime.value();game.set_save_directory(pref_path);Result<void> setup;
 GraphicsControl graphics(settings);HelpCard help;bool menu_open=false,settings_dirty=true;
 std::optional<Pack> original,remastered;
 if(setup.has_value()&&!settings.developer_nearest){auto catalog=source_catalog(std::filesystem::u8path(options.data_path));if(!catalog.has_value())setup=catalog.error();else {
  auto raw=read_file(std::filesystem::u8path(options.data_path)/"HL.EXE");if(!raw.has_value()||raw.value().size()<252284+4096)setup=DataError{"","font",0,"missing original font source"};
  auto pack=setup.has_value()?Pack::open(std::filesystem::u8path(options.original_pack),catalog.value(),sha256(ByteView(raw.value()).subspan(252284,4096))):Result<Pack>{setup.error()};if(!pack.has_value())setup=pack.error();else original=std::move(pack.value());
  if(setup.has_value()&&!options.remastered_pack.empty()){auto root=std::filesystem::u8path(options.remastered_pack);SourceCatalog approved;for(const auto& [name,source]:catalog.value())if(std::filesystem::exists(root/"flc"/name/"index.json"))approved.emplace(name,source);auto replacements=Pack::open(root,approved);if(!replacements.has_value())setup=replacements.error();else remastered=std::move(replacements.value());}
 }}
 auto font_bytes=read_file(std::filesystem::u8path(options.data_path)/"HL.EXE");auto font=font_bytes.has_value()?load_original_font({font_bytes.value(),252284}):Result<BitmapFont>{font_bytes.error()};if(!font.has_value())setup=font.error();
 nlohmann::json fade_steps=nlohmann::json::array();
 auto draw_game=[&]()->Result<void>{
  renderer.value()->set_scaling(graphics.settings().scaling);
  renderer.value()->scene(game.scene());
  auto surfaces=game.render_surfaces(bool(original));if(!surfaces.has_value())return surfaces.error();
  auto palette=game.palette_view();auto palette_hash=sha256(std::as_bytes(std::span(palette.source)));
  bool ready=true;
  if(original&&graphics.mode()!=graphics.requested_mode()){FrameResolver resolver(*original,remastered?&*remastered:nullptr);auto loaded=renderer.value()->ready(surfaces.value(),resolver,graphics.requested_mode(),palette_hash);if(!loaded.has_value())return loaded.error();ready=loaded.value();}
  bool changed=graphics.frame_boundary(ready);if(changed||settings_dirty){auto saved=save_graphics_settings(settings_file,graphics.settings());if(!saved.has_value())return saved;settings_dirty=false;}
  auto markers=graphics.hints_visible()?hint_surfaces(game.active_areas(),true):std::vector<RenderSurface>{};for(auto& marker:markers)surfaces.value().push_back(std::move(marker));if(menu_open){auto usable=usable_window_size(window);if(!usable.has_value())return usable.error();auto panel=options_surfaces(options_rows(graphics.settings(),usable.value()),font.value(),bool(original));for(auto& surface:panel)surfaces.value().push_back(std::move(surface));}
  if(game.scene()=="14rf"&&!menu_open){auto usable=usable_window_size(window);if(!usable.has_value())return usable.error();auto rows=game_settings_rows(graphics.settings(),usable.value(),font.value());for(auto& surface:game_settings_surfaces(rows,font.value()))surfaces.value().push_back(std::move(surface));}
  for(auto& surface:help_surfaces(font.value(),help.alpha(SDL_GetTicks()),graphics.settings()))surfaces.value().push_back(std::move(surface));
  if(!original)return renderer.value()->present(std::move(surfaces.value()));
  auto effect=palette_postprocess(palette);if(!effect.has_value())return effect.error();FrameResolver resolver(*original,remastered?&*remastered:nullptr);return renderer.value()->present(std::move(surfaces.value()),resolver,graphics.mode(),palette_hash,effect.value());
 };
 game.set_cooperative_fades(true);
 game.set_palette_presenter([&](unsigned step)->Result<void>{auto now=SDL_GetTicks();fade_steps.push_back({{"step",step},{"update_ms",now}});return {};});
 Result<void> result=setup.has_value()?(options.load_save.empty()?game.start():Result<void>{}):setup;std::optional<Replay> replay;uint64_t end_ms=0;
 if(result.has_value()&&!options.replay_path.empty()) {
  auto input=read_trace(options.replay_path);if(!input.has_value())result=input.error();else {end_ms=input.value().empty()?18000:input.value().back().tick+3000;replay.emplace(std::move(input.value()));}
  // The reference route starts at the stable menu after startup, as recorded in M0.
  if(options.load_save.empty()){
   for(unsigned warmup=0;result.has_value()&&(game.scene()!="126y"||game.fading())&&warmup<10000;++warmup)result=game.update();
   if(result.has_value()&&(game.scene()!="126y"||game.fading()))result=DataError{"",game.scene(),0,"reference menu synchronization failed"};
  }
 }
 auto output=std::filesystem::u8path(options.checkpoint_out);if(!output.empty())std::filesystem::create_directories(output);
 unsigned quick_saves=0,quick_loads=0;
 StartupRenderBudget startup_budget;
 nlohmann::json runtime_frames=nlohmann::json::array();uint64_t max_update_backlog=0;auto prior_frame=std::chrono::steady_clock::now();
 auto input_event=[&](const SDL_Event& event)->Result<void>{
  if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN)help.dismiss();
  // Diagnostics: HL_LOG_KEYS=<file> records every key event (support for players' keyboard problems).
  if((event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)&&SDL_getenv("HL_LOG_KEYS")){if(auto* f=std::fopen(SDL_getenv("HL_LOG_KEYS"),"a")){std::fprintf(f,"%s key=%u name=%s scancode=%d mod=%04x repeat=%d editing=%d menu=%d\n",event.type==SDL_EVENT_KEY_DOWN?"down":"up",unsigned(event.key.key),SDL_GetKeyName(event.key.key),int(event.key.scancode),unsigned(event.key.mod),int(event.key.repeat),int(game.editing_save_name()),int(menu_open));std::fclose(f);}}
  bool show_help=help_shortcut(event,game.editing_save_name());if(show_help)help.show(SDL_GetTicks());
  auto changed_window=window_event(window,graphics,event);if(!changed_window.has_value())return changed_window.error();if(changed_window.value()){settings_dirty=true;return {};}
  if(graphics_event(graphics,event,game.editing_save_name())||show_help)return {};
  if(options_shortcut(event,game.editing_save_name())){menu_open=!menu_open;if(!(menu_open?SDL_ShowCursor():SDL_HideCursor()))return DataError{"","options cursor",0,SDL_GetError()};return {};}
  if(menu_open&&event.type==SDL_EVENT_KEY_DOWN&&event.key.key==SDLK_ESCAPE){menu_open=false;if(!SDL_HideCursor())return DataError{"","options cursor",0,SDL_GetError()};return {};}
  if(menu_open){
   if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&event.button.button==SDL_BUTTON_LEFT){
    int w,h,pw,ph;if(!SDL_GetWindowSize(window,&w,&h)||!SDL_GetRenderOutputSize(renderer.value()->sdl(),&pw,&ph))return DataError{"","options pointer",0,SDL_GetError()};
    auto point=Viewport::window_to_logical({event.button.x,event.button.y},{w,h,pw,ph},graphics.settings().scaling);
    auto usable=usable_window_size(window);if(!usable.has_value())return usable.error();
    auto row=point?option_at(options_rows(graphics.settings(),usable.value()),*point):std::nullopt;
    if(row){if(row->action==OptionAction::Close){menu_open=false;if(!SDL_HideCursor())return DataError{"","options cursor",0,SDL_GetError()};}
     else {auto applied=apply_option(window,graphics,*row);if(!applied.has_value())return applied;settings_dirty=true;}}
   }
   return {};
  }
  if(game.scene()=="14rf"&&event.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&event.button.button==SDL_BUTTON_LEFT){
   int w,h,pw,ph;if(!SDL_GetWindowSize(window,&w,&h)||!SDL_GetRenderOutputSize(renderer.value()->sdl(),&pw,&ph))return DataError{"","settings pointer",0,SDL_GetError()};
   auto point=Viewport::window_to_logical({event.button.x,event.button.y},{w,h,pw,ph},graphics.settings().scaling);
   auto usable=usable_window_size(window);if(!usable.has_value())return usable.error();
   auto row=point?option_at(game_settings_rows(graphics.settings(),usable.value(),font.value()),*point):std::nullopt;
   if(row){if(row->action==OptionAction::Help)help.show(SDL_GetTicks());else {auto applied=apply_option(window,graphics,*row);if(!applied.has_value())return applied;settings_dirty=true;}return {};}
  }
  if(event.type==SDL_EVENT_TEXT_INPUT)return game.text_input(event.text.text);
  if(event.type==SDL_EVENT_KEY_DOWN&&game.editing_save_name()){if(event.key.key==SDLK_RETURN)return game.edit_key("enter");if(event.key.key==SDLK_BACKSPACE)return game.edit_key("backspace");if(event.key.key==SDLK_ESCAPE)return game.edit_key("escape");return {};}
  if(event.type==SDL_EVENT_KEY_DOWN){if(event.key.key==SDLK_ESCAPE){if(!event.key.repeat)return game.escape();return {};}
   auto file=pref_path/"scene.hls";
   if(event.key.key==SDLK_F2&&game.can_save()){auto r=game.save(file);if(r.has_value())++quick_saves;return r;}
   if(event.key.key==SDLK_F3&&std::filesystem::exists(file)){auto r=game.load(file);if(r.has_value())++quick_loads;return r;}
  }
  if(event.type==SDL_EVENT_MOUSE_MOTION||event.type==SDL_EVENT_MOUSE_BUTTON_DOWN)return dispatch_runtime_mouse(game,window,renderer.value()->sdl(),event,graphics.settings().scaling);
  return {};
 };
 uint64_t start=SDL_GetTicks(),updates=0,last_capture=UINT64_MAX,submitted_frames=0;bool quit=false;
 help.launch(graphics,start);
 std::ofstream audio_file;if(!output.empty())audio_file.open(output/"queued-audio.f32",std::ios::binary);
 while(result.has_value()&&!quit&&!game.quit_requested()) {
  auto frame_begin=std::chrono::steady_clock::now();auto updates_before=updates;
  uint64_t elapsed=SDL_GetTicks()-start;
  if(game.editing_save_name())graphics.focus_lost();
  if(game.editing_save_name()&&!SDL_TextInputActive(window))SDL_StartTextInput(window);else if(!game.editing_save_name()&&SDL_TextInputActive(window))SDL_StopTextInput(window);
  SDL_Event event;
  while(SDL_PollEvent(&event)) {
   if(event.type==SDL_EVENT_QUIT){quit=true;break;}
   if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){graphics.focus_lost();continue;}
   if(replay&&event.type!=SDL_EVENT_WINDOW_RESIZED&&event.type!=SDL_EVENT_WINDOW_ENTER_FULLSCREEN&&event.type!=SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)continue;
   result=input_event(event);if(!result.has_value())break;
  }
  uint64_t wanted=elapsed*35/1000;max_update_backlog=std::max(max_update_backlog,wanted-updates);
  if(wanted-updates>350){result=DataError{"",game.scene(),0,"native runtime fell more than ten seconds behind"};break;}
  while(result.has_value()&&updates<wanted) {
   uint64_t event_ms=updates*1000/35;
   if(replay)for(const auto& e:replay->events_at(event_ms)) {
    SDL_Event input{};
    if(e.kind=="key_down"||e.kind=="key_up"){input.type=e.kind=="key_down"?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;input.key.key=e.key=="?"?SDLK_QUESTION:SDL_GetKeyFromName(e.key.c_str());}
    else if(e.kind=="text_input"){input.type=SDL_EVENT_TEXT_INPUT;input.text.text=e.text.c_str();}
    else {int w,h,pw,ph;SDL_GetWindowSize(window,&w,&h);SDL_GetRenderOutputSize(renderer.value()->sdl(),&pw,&ph);auto v=Viewport::fit(pw,ph,graphics.settings().scaling);float x=float((v.x+(e.logical_x+0.5)*v.width/640)*w/pw),y=float((v.y+(e.logical_y+0.5)*v.height/480)*h/ph);
     if(e.kind=="mouse_move"){input.type=SDL_EVENT_MOUSE_MOTION;input.motion.x=x;input.motion.y=y;}
     else {input.type=e.kind=="mouse_down"?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;input.button.x=x;input.button.y=y;input.button.button=e.button=="left"?SDL_BUTTON_LEFT:e.button=="right"?SDL_BUTTON_RIGHT:SDL_BUTTON_MIDDLE;}
    }
    result=input_event(input);
    if(!result.has_value())break;
   }
   if(!result.has_value())break;result=game.update();++updates;if(!result.has_value())break;
   auto audio=game.audio();if(!SDL_PutAudioStreamData(stream,audio.data(),int(audio.size_bytes()))){result=DataError{"","audio",0,SDL_GetError()};break;}
   if(audio_file)audio_file.write(reinterpret_cast<const char*>(audio.data()),std::streamsize(audio.size_bytes()));submitted_frames+=audio.size()/2;
  }
  auto update_end=std::chrono::steady_clock::now();
  if(!result.has_value())break;
  auto uploads_before=renderer.value()->texture_uploads(),evictions_before=renderer.value()->texture_evictions();
  result=draw_game();if(!result.has_value())break;
  auto draw_end=std::chrono::steady_clock::now();
  uint64_t bucket=elapsed/1000;
  if(!output.empty()&&last_capture!=bucket) {
   auto checkpoint=nlohmann::json::parse(canonical_checkpoint(game.checkpoint()));checkpoint["help_visible"]=help.visible(SDL_GetTicks());checkpoint["help_alpha"]=help.alpha(SDL_GetTicks());checkpoint["help_launches"]=graphics.settings().help_launches;checkpoint["options_open"]=menu_open;checkpoint["cursor_visible"]=SDL_CursorVisible();checkpoint["hints_visible"]=graphics.hints_visible();checkpoint["display_settings"]={{"fullscreen",graphics.settings().fullscreen},{"window_width",graphics.settings().window_width},{"window_height",graphics.settings().window_height},{"scaling",graphics.settings().scaling==ScalingMode::Fill?"fill":graphics.settings().scaling==ScalingMode::Whole?"whole":"stretch"},{"active_area_hints",graphics.settings().hints_enabled}};checkpoint["graphics"]=graphics.mode()==GraphicsMode::Original?"Original":"Remastered";checkpoint["id"]="tick-"+std::to_string(bucket*1000);checkpoint["route_ms"]=elapsed;checkpoint["source_script"]=game.scene();checkpoint["geometry"]=game.geometry();checkpoint["can_save"]=game.can_save();checkpoint["editing_save_name"]=game.editing_save_name();checkpoint["selected_item"]=game.selected_item();checkpoint["inventory_open"]=game.inventory_open();
   auto path=output/("tick-"+std::to_string(bucket*1000));auto captured=renderer.value()->readback();
   if(!captured.has_value()){result=captured.error();break;}
   auto model=game.rgba();if(!model.has_value()){result=model.error();break;}
   checkpoint["gpu_readback_equals_cpu"]=captured.value().width==640&&captured.value().height==480&&captured.value().pixels==model.value();checkpoint["capture_dimensions"]={captured.value().width,captured.value().height};checkpoint["screenshot"]=path.filename().string()+".png";
   if(!stbi_write_png((path.string()+".png").c_str(),captured.value().width,captured.value().height,4,captured.value().pixels.data(),captured.value().width*4)){result=DataError{"","capture",0,"PNG write failed"};break;}
   result=write_json(path.string()+".json",checkpoint);last_capture=bucket;
  }
  auto capture_end=std::chrono::steady_clock::now();
  if(!SDL_RenderPresent(renderer.value()->sdl())){result=DataError{"","renderer",0,SDL_GetError()};break;}
  auto present_end=std::chrono::steady_clock::now();
  auto ms=[](auto begin,auto end){return std::chrono::duration<double,std::milli>(end-begin).count();};
  if(!options.report_path.empty())runtime_frames.push_back({{"route_ms",elapsed},{"interval_ms",ms(prior_frame,frame_begin)},{"work_ms",ms(frame_begin,present_end)},{"updates",updates-updates_before},{"update_input_audio_ms",ms(frame_begin,update_end)},{"draw_ms",ms(update_end,draw_end)},{"capture_ms",ms(draw_end,capture_end)},{"present_ms",ms(capture_end,present_end)},{"texture_uploads",renderer.value()->texture_uploads()-uploads_before},{"texture_evictions",renderer.value()->texture_evictions()-evictions_before},{"scene",game.scene()}});
#ifdef _WIN32
  // Count rendering only: scene IO, fades and captures cannot condemn the GPU.
  if(preference=="auto"&&renderer.value()->backend().starts_with("gpu/")&&startup_budget.observe(ms(update_end,draw_end)+ms(capture_end,present_end))){
   result=renderer.value()->use_software("startup rendering exceeded 35 Hz budget in at least 30 of 60 frames after 30 warmup frames");if(!result.has_value())break;
  }
#endif
  prior_frame=frame_begin;if(replay&&elapsed>=end_ms)quit=true;SDL_Delay(1);
 }
 if(result.has_value()&&settings_dirty)result=save_graphics_settings(settings_file,graphics.settings());
 if(audio_file){audio_file.close();if(!audio_file)result=DataError{"","audio capture",0,"audio capture write failed"};}
 if(!options.opcode_trace_path.empty()){std::ofstream out(std::filesystem::u8path(options.opcode_trace_path));for(const auto& entry:game.trace())out<<entry.dump()<<'\n';if(!out)result=DataError{"","trace",0,"opcode trace write failed"};}
 nlohmann::json report{{"macos_fullscreen_spaces",SDL_GetHintBoolean(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES,true)},{"runtime_frames",runtime_frames},{"max_update_backlog",max_update_backlog},{"runtime_timing_scope","actual loop including updates, input, audio submission, composition, present and any requested captures; frame intervals also include sleep"},{"options_open",menu_open},{"cursor_visible",SDL_CursorVisible()},{"quick_saves",quick_saves},{"quick_loads",quick_loads},{"schema_version",1},{"platform",SDL_GetPlatform()},{"sdl_version",SDL_GetVersion()},{"backend",renderer.value()->backend()},{"gpu_failure",renderer.value()->gpu_failure()},{"adapter",renderer.value()->adapter()},{"renderer_preference",preference},{"fade_steps",fade_steps},{"updates",updates},{"elapsed_ms",SDL_GetTicks()-start},{"audio_submitted_frames",submitted_frames},{"audio_rate",48000},{"audio_channels",2},{"audio_queued_bytes_at_exit",SDL_GetAudioStreamQueued(stream)},{"scene",game.scene()},{"graphics",graphics.mode()==GraphicsMode::Original?"Original":"Remastered"},{"texture_working_set_bytes",renderer.value()->cache_bytes()},{"exit_code",result.has_value()?0:1},{"capture_method","SDL GPU/renderer readback before present"},{"synchronization",replay?"original startup scripts advanced to stable 126y menu before timed route":"normal startup"}};
 if(!result.has_value()){auto e=result.error();report["error"]={{"object",e.resource},{"offset",e.byte_offset},{"reason",e.reason}};std::cerr<<e.resource<<":"<<e.byte_offset<<" "<<e.reason<<'\n';}
 SDL_DestroyAudioStream(stream);renderer.value().reset();SDL_DestroyWindow(window);SDL_Quit();report["resources_released"]=true;
 if(!options.report_path.empty()){auto saved=write_json(std::filesystem::u8path(options.report_path),report);if(!saved.has_value())return 1;}
 return result.has_value()?0:1;
}
}
