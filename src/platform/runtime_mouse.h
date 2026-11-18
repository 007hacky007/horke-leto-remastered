#pragma once
#include "script/intro_runtime.h"
#include "render/viewport.h"
#include <SDL3/SDL.h>
namespace hl {
inline Result<void> dispatch_runtime_mouse(IntroRuntime& game,SDL_Window* window,SDL_Renderer* renderer,const SDL_Event& event,ScalingMode mode=ScalingMode::Fill){
 if(event.type!=SDL_EVENT_MOUSE_MOTION&&event.type!=SDL_EVENT_MOUSE_BUTTON_DOWN)return {};
 int w,h,pw,ph;if(!SDL_GetWindowSize(window,&w,&h)||!SDL_GetRenderOutputSize(renderer,&pw,&ph))return DataError{"","pointer",0,SDL_GetError()};
 auto point=Viewport::window_to_logical({event.type==SDL_EVENT_MOUSE_MOTION?event.motion.x:event.button.x,event.type==SDL_EVENT_MOUSE_MOTION?event.motion.y:event.button.y},{w,h,pw,ph},mode);
 if(!point)return {};auto moved=game.pointer(point->x,point->y);if(!moved.has_value())return moved;
 return event.type==SDL_EVENT_MOUSE_BUTTON_DOWN?game.click(point->x,point->y,event.button.button==SDL_BUTTON_RIGHT):Result<void>{};
}
}
