// Build progress on the app's Dock icon (macOS) or taskbar button (Windows).
#pragma once
struct SDL_Window;

namespace hb {
enum class TaskbarState { None, Normal, Error };
// fraction 0..1; None clears the indicator. Call from the UI thread only.
void taskbar_progress(SDL_Window* window, TaskbarState state, double fraction);
// Draws attention when the build finishes while the window is in the background.
void taskbar_attention(SDL_Window* window);
}
