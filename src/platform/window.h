#pragma once
#include "render/graphics_mode.h"
#include <SDL3/SDL.h>
namespace hl {
inline bool fullscreen_shortcut(const SDL_Event& event){
 if(event.type!=SDL_EVENT_KEY_DOWN||event.key.repeat)return false;
 if(event.key.key==SDLK_F11)return true;
 if(event.key.key==SDLK_RETURN&&(event.key.mod&SDL_KMOD_ALT))return true;
#ifdef __APPLE__
 if(event.key.key==SDLK_F&&(event.key.mod&SDL_KMOD_GUI)&&(event.key.mod&SDL_KMOD_CTRL))return true;
#endif
 return false;
}
inline Result<void> configure_window(SDL_Window* window,GraphicsSettings& settings){
 SDL_Rect usable{};if(!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window),&usable))return DataError{"","display",0,SDL_GetError()};
 int top=0,left=0,bottom=0,right=0;SDL_GetWindowBordersSize(window,&top,&left,&bottom,&right);
 auto size=choose_window_size(usable.w-left-right,usable.h-top-bottom,settings);
 settings.window_width=size.width;settings.window_height=size.height;
 if(!SDL_SetWindowSize(window,size.width,size.height)||!SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED)||
    !SDL_SetWindowFullscreen(window,settings.fullscreen)||!SDL_SyncWindow(window))return DataError{"","window",0,SDL_GetError()};
 return {};
}
inline Result<bool> window_event(SDL_Window* window,GraphicsControl& graphics,const SDL_Event& event){
 const auto& settings=graphics.settings();
 if(fullscreen_shortcut(event)){
  bool fullscreen=!(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN);int width=settings.window_width,height=settings.window_height;
  if(fullscreen&&!SDL_GetWindowSize(window,&width,&height))return DataError{"","window size",0,SDL_GetError()};
  if(!SDL_SetWindowFullscreen(window,fullscreen)||!SDL_SyncWindow(window))return DataError{"","fullscreen",0,SDL_GetError()};
  graphics.set_window(fullscreen,width,height);return true;
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
