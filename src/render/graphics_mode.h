#pragma once
#include "render/pack.h"
namespace hl {
struct GraphicsSettings {GraphicsMode mode{GraphicsMode::Original};std::string renderer="auto";std::string switch_key="F8",hints_key="Space";bool hints_enabled{true},developer_nearest{},fullscreen{};int window_width{},window_height{};};
struct WindowSize {int width{},height{};bool operator==(const WindowSize&)const=default;};
WindowSize choose_window_size(int usable_width,int usable_height,const GraphicsSettings&);
Result<GraphicsSettings> load_graphics_settings(const std::filesystem::path&);
Result<void> save_graphics_settings(const std::filesystem::path&,const GraphicsSettings&);
std::string graphics_menu(const GraphicsSettings&);
class GraphicsControl {
 GraphicsSettings settings_;GraphicsMode requested_;bool held_{};
public:
 explicit GraphicsControl(GraphicsSettings settings):settings_(std::move(settings)),requested_(settings_.mode){}
 bool key(std::string_view key,bool down,bool repeat);
 void toggle(){requested_=requested_==GraphicsMode::Original?GraphicsMode::Remastered:GraphicsMode::Original;}
 bool frame_boundary(bool ready=true){if(!ready)return false;bool changed=settings_.mode!=requested_;settings_.mode=requested_;return changed;}
 void set_window(bool fullscreen,int width,int height){settings_.fullscreen=fullscreen;settings_.window_width=width;settings_.window_height=height;}
 void focus_lost(){held_=false;}
 void set_hints_enabled(bool enabled){settings_.hints_enabled=enabled;if(!enabled)held_=false;}
 bool hints_visible()const{return settings_.hints_enabled&&held_;}
 GraphicsMode mode()const{return settings_.mode;}
 GraphicsMode requested_mode()const{return requested_;}
 const GraphicsSettings& settings()const{return settings_;}
};
}
