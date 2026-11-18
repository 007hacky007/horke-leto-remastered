#include "render/graphics_mode.h"
#include <SDL3/SDL.h>
#import <AppKit/AppKit.h>
namespace hl {
WindowSize cocoa_window_borders(SDL_Window* window){
 auto native=(__bridge NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,nullptr);
 auto frame=native.frame;auto content=[native contentRectForFrameRect:frame];
 return {int(frame.size.width-content.size.width),int(frame.size.height-content.size.height)};
}
}
