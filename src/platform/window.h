#pragma once
#include "platform/options_panel.h"
#include <SDL3/SDL.h>
namespace hl {
inline bool fullscreen_shortcut(const SDL_Event& event){
 if(event.type!=SDL_EVENT_KEY_DOWN||event.key.repeat)return false;
 if(event.key.key==SDLK_F11)return true;
 if(event.key.key==SDLK_RETURN&&(event.key.mod&SDL_KMOD_ALT))return true;
#ifdef __APPLE__
 if(event.key.key==SDLK_RETURN&&(event.key.mod&SDL_KMOD_GUI))return true;
 if(event.key.key==SDLK_F&&(event.key.mod&SDL_KMOD_GUI)&&(event.key.mod&SDL_KMOD_CTRL))return true;
#endif
 return false;
}
inline bool options_shortcut(const SDL_Event& event,bool editing){
 if(event.type!=SDL_EVENT_KEY_DOWN||event.key.repeat)return false;
 if(event.key.key==SDLK_F10)return true;
 if(editing)return false;
 if(event.key.key==SDLK_O&&!(event.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_ALT|SDL_KMOD_GUI)))return true;
#ifdef __APPLE__
 if(event.key.key==SDLK_COMMA&&(event.key.mod&SDL_KMOD_GUI))return true;
#endif
 return false;
}
inline bool graphics_event(GraphicsControl& graphics,const SDL_Event& event,bool editing){
 if(event.type!=SDL_EVENT_KEY_DOWN&&event.type!=SDL_EVENT_KEY_UP)return false;
 if(event.type==SDL_EVENT_KEY_DOWN&&(event.key.mod&(SDL_KMOD_GUI|SDL_KMOD_CTRL|SDL_KMOD_ALT)))return false;
 auto key=std::string_view(SDL_GetKeyName(event.key.key));
 if(editing&&(key.size()==1||event.key.key==SDLK_SPACE))return false;
 if(event.key.key==SDLK_G){if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat)graphics.toggle();return true;}
 return graphics.key(key,event.type==SDL_EVENT_KEY_DOWN,event.key.repeat);
}
#ifdef __APPLE__
WindowSize cocoa_window_borders(SDL_Window* window);
#endif
inline Result<WindowSize> usable_window_size(SDL_Window* window){
 auto display=SDL_GetDisplayForWindow(window);SDL_Rect usable{};if(!SDL_GetDisplayUsableBounds(display,&usable))return DataError{"","display",0,SDL_GetError()};
 auto props=SDL_GetWindowProperties(window);
 if(!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)){
  SDL_SetNumberProperty(props,"hl.windowed_display",display);SDL_SetNumberProperty(props,"hl.windowed_usable_width",usable.w);SDL_SetNumberProperty(props,"hl.windowed_usable_height",usable.h);
 }else if(SDL_GetNumberProperty(props,"hl.windowed_display",0)==display){
  // Spaces hides the menu bar; presets must still fit after leaving fullscreen.
  usable.w=int(SDL_GetNumberProperty(props,"hl.windowed_usable_width",usable.w));usable.h=int(SDL_GetNumberProperty(props,"hl.windowed_usable_height",usable.h));
 }
 if(!(SDL_GetWindowFlags(window)&(SDL_WINDOW_FULLSCREEN|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_MINIMIZED))){
  int top=0,left=0,bottom=0,right=0;WindowSize borders;
  if(SDL_GetWindowBordersSize(window,&top,&left,&bottom,&right))borders={left+right,top+bottom};
#ifdef __APPLE__
  else borders=cocoa_window_borders(window);
#endif
  SDL_SetNumberProperty(props,"hl.window_border_width",borders.width);SDL_SetNumberProperty(props,"hl.window_border_height",borders.height);
 }
 return WindowSize{usable.w-int(SDL_GetNumberProperty(props,"hl.window_border_width",0)),usable.h-int(SDL_GetNumberProperty(props,"hl.window_border_height",0))};
}
inline Result<void> toggle_fullscreen(SDL_Window* window,GraphicsControl& graphics){
 bool fullscreen=!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN);int width=graphics.settings().window_width,height=graphics.settings().window_height;
 if(fullscreen&&!SDL_GetWindowSize(window,&width,&height))return DataError{"","window size",0,SDL_GetError()};
 if(!SDL_SetWindowFullscreen(window,fullscreen)||!SDL_SyncWindow(window))return DataError{"","fullscreen",0,SDL_GetError()};
 graphics.set_window(fullscreen,width,height);return {};
}
inline Result<void> apply_option(SDL_Window* window,GraphicsControl& graphics,const OptionRow& row){
 if(!row.enabled)return {};
 switch(row.action){
 case OptionAction::Graphics:graphics.toggle();break;
 case OptionAction::Fullscreen:return toggle_fullscreen(window,graphics);
 case OptionAction::Size:{
  auto usable=usable_window_size(window);if(!usable.has_value())return usable.error();
  if(row.size.width<=0||row.size.height<=0||row.size.width>usable.value().width||row.size.height>usable.value().height)return {};
  if(!SDL_SetWindowFullscreen(window,false)||!SDL_SyncWindow(window))return DataError{"","window size",0,SDL_GetError()};
  if((SDL_GetWindowFlags(window)&SDL_WINDOW_MAXIMIZED)&&(!SDL_RestoreWindow(window)||!SDL_SyncWindow(window)))return DataError{"","window restore",0,SDL_GetError()};
  if(!SDL_SetWindowSize(window,row.size.width,row.size.height)||!SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED)||!SDL_SyncWindow(window))return DataError{"","window size",0,SDL_GetError()};
  graphics.set_window(false,row.size.width,row.size.height);break;
 }
 case OptionAction::Scaling:graphics.set_scaling(row.scaling);break;
 case OptionAction::Hints:graphics.set_hints_enabled(!graphics.settings().hints_enabled);break;
 default:break;
 }
 return {};
}
inline Result<void> configure_window(SDL_Window* window,GraphicsSettings& settings){
 auto usable=usable_window_size(window);if(!usable.has_value())return usable.error();
 auto size=choose_window_size(usable.value().width,usable.value().height,settings);
 settings.window_width=size.width;settings.window_height=size.height;
 if(!SDL_SetWindowSize(window,size.width,size.height)||!SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED)||
    !SDL_SetWindowFullscreen(window,settings.fullscreen)||!SDL_SyncWindow(window))return DataError{"","window",0,SDL_GetError()};
 return {};
}
inline Result<bool> window_event(SDL_Window* window,GraphicsControl& graphics,const SDL_Event& event){
 const auto& settings=graphics.settings();
 if(fullscreen_shortcut(event)){
  auto changed=toggle_fullscreen(window,graphics);if(!changed.has_value())return changed.error();return true;
 }
 if(event.type==SDL_EVENT_WINDOW_ENTER_FULLSCREEN||event.type==SDL_EVENT_WINDOW_LEAVE_FULLSCREEN){
  bool fullscreen=bool(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN);
  if(fullscreen!=settings.fullscreen){graphics.set_window(fullscreen,settings.window_width,settings.window_height);return true;}
 }
 if(event.type==SDL_EVENT_WINDOW_RESIZED&&!(SDL_GetWindowFlags(window)&(SDL_WINDOW_FULLSCREEN|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_MINIMIZED))&&!settings.fullscreen){
  int width=0,height=0;if(!SDL_GetWindowSize(window,&width,&height))return DataError{"","window size",0,SDL_GetError()};
  if(width>0&&height>0&&(width!=settings.window_width||height!=settings.window_height)){graphics.set_window(false,width,height);return true;}
 }
 return false;
}
}
