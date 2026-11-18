#include "render/graphics_mode.h"
#include "render/renderer_policy.h"
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace hl {
namespace {
bool valid_window(const GraphicsSettings& s){return (s.window_width==0&&s.window_height==0)||(s.window_width>0&&s.window_height>0);}
bool valid_key(const std::string& key){return !key.empty()&&key.size()<=32&&std::all_of(key.begin(),key.end(),[](unsigned char c){return c>=32&&c<127;});}
}
std::vector<WindowSize> window_size_presets(int width,int height){
 std::vector<WindowSize> out;for(auto size:{WindowSize{640,480},{960,720},{1280,960},{1600,1200},{1920,1440}})if(size.width<=width&&size.height<=height)out.push_back(size);return out;
}
WindowSize choose_window_size(int width,int height,const GraphicsSettings& s){
 width=std::max(1,width);height=std::max(1,height);
 if(s.window_width>0&&s.window_height>0)return {std::min(width,s.window_width),std::min(height,s.window_height)};
 int scale=std::max(1,std::min(width/640,height/480));return {std::min(width,640*scale),std::min(height,480*scale)};
}
Result<GraphicsSettings> load_graphics_settings(const std::filesystem::path& path){
 try{std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size>16384)return DataError{"","graphics settings",0,"missing or excessive settings file"};std::ifstream input(path);auto j=nlohmann::json::parse(input);if(j.at("schema_version")!=1)return DataError{"","graphics settings",0,"unsupported settings version"};GraphicsSettings out;if(j.contains("help_launches")){const auto& count=j.at("help_launches");if(!count.is_number_integer()||count.get<double>()<0||count.get<double>()>UINT32_MAX)return DataError{"","graphics settings",0,"invalid help launch count"};out.help_launches=count.get<uint32_t>();}auto scaling=j.value("scaling","fill");if(scaling!="fill"&&scaling!="whole"&&scaling!="stretch")return DataError{"","graphics settings",0,"invalid scaling mode"};out.scaling=scaling=="whole"?ScalingMode::Whole:scaling=="stretch"?ScalingMode::Stretch:ScalingMode::Fill;auto mode=j.at("graphics").get<std::string>();if(mode!="Original"&&mode!="Remastered")return DataError{"","graphics settings",0,"invalid graphics mode"};out.renderer=j.value("renderer","auto");if(!valid_renderer_preference(out.renderer))return DataError{"","graphics settings",0,"invalid renderer preference"};out.mode=mode=="Original"?GraphicsMode::Original:GraphicsMode::Remastered;out.switch_key=j.value("switch_graphics","F8");out.hints_key=j.value("show_active_areas","Space");out.hints_enabled=j.value("active_area_hints",true);out.developer_nearest=j.value("developer_nearest",false);out.fullscreen=j.value("fullscreen",false);out.window_width=j.value("window_width",0);out.window_height=j.value("window_height",0);if(!valid_window(out))return DataError{"","graphics settings",0,"invalid window size"};if(!valid_key(out.switch_key)||!valid_key(out.hints_key)||out.switch_key==out.hints_key)return DataError{"","graphics settings",0,"invalid or conflicting key binding"};return out;
 }catch(const std::exception& e){return DataError{"","graphics settings",0,e.what()};}
}
Result<void> save_graphics_settings(const std::filesystem::path& path,const GraphicsSettings& s){
 if(s.scaling!=ScalingMode::Fill&&s.scaling!=ScalingMode::Whole&&s.scaling!=ScalingMode::Stretch)return DataError{"","graphics settings",0,"invalid scaling mode"};
 if(!valid_window(s))return DataError{"","graphics settings",0,"invalid window size"};
 if(!valid_renderer_preference(s.renderer))return DataError{"","graphics settings",0,"invalid renderer preference"};
 if(!valid_key(s.switch_key)||!valid_key(s.hints_key)||s.switch_key==s.hints_key)return DataError{"","graphics settings",0,"invalid or conflicting key binding"};
 auto temporary=path;temporary+=".tmp";std::error_code ec;if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path(),ec);if(ec)return DataError{"","graphics settings",0,"cannot create settings directory"};
 {std::ofstream out(temporary);out<<nlohmann::json{{"schema_version",1},{"help_launches",s.help_launches},{"scaling",s.scaling==ScalingMode::Fill?"fill":s.scaling==ScalingMode::Whole?"whole":"stretch"},{"renderer",s.renderer},{"graphics",s.mode==GraphicsMode::Original?"Original":"Remastered"},{"switch_graphics",s.switch_key},{"show_active_areas",s.hints_key},{"active_area_hints",s.hints_enabled},{"developer_nearest",s.developer_nearest},{"fullscreen",s.fullscreen},{"window_width",s.window_width},{"window_height",s.window_height}}.dump(2)<<'\n';out.close();if(!out)return DataError{"","graphics settings",0,"cannot write settings"};}
#ifdef _WIN32
 if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return DataError{"","graphics settings",0,"cannot replace settings"};
#else
 std::filesystem::rename(temporary,path,ec);if(ec)return DataError{"","graphics settings",0,"cannot replace settings"};
#endif
 return {};
}
std::string graphics_menu(const GraphicsSettings& s){
 std::string text="Graphics: Original / Remastered\nSwitch graphics: "+s.switch_key+" / G\nShow active areas: hold "+s.hints_key;
#ifdef __APPLE__
 return text+"\nFullscreen: Cmd+Ctrl+F / Cmd+Enter / F11\nOptions: O / Cmd+comma";
#else
 return text+"\nFullscreen: Alt+Enter / F11\nOptions: O";
#endif
}
bool GraphicsControl::key(std::string_view key,bool down,bool repeat){if(key==settings_.hints_key){held_=down;return true;}if(key==settings_.switch_key){if(down&&!repeat)toggle();return true;}return false;}
}
