// Progress on the taskbar button (ITaskbarList3), like Explorer's copy dialog.
#include "taskbar.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <shobjidl.h>

namespace hb {
static ITaskbarList3* list = nullptr;
static bool tried = false;
static int shown = -2;

static HWND handle(SDL_Window* window) {
    return HWND(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

static ITaskbarList3* taskbar() {
    if (!tried) {
        tried = true;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3, reinterpret_cast<void**>(&list))) ||
            FAILED(list->HrInit())) {
            if (list) list->Release();
            list = nullptr;
        }
    }
    return list;
}

void taskbar_progress(SDL_Window* window, TaskbarState state, double fraction) {
    auto* t = taskbar();
    HWND hwnd = handle(window);
    if (!t || !hwnd) return;
    int permille = state == TaskbarState::None ? -1 : state == TaskbarState::Error ? 2000 : int(fraction * 1000);
    if (permille == shown) return;
    shown = permille;
    if (state == TaskbarState::None) { t->SetProgressState(hwnd, TBPF_NOPROGRESS); return; }
    t->SetProgressState(hwnd, state == TaskbarState::Error ? TBPF_ERROR : TBPF_NORMAL);
    t->SetProgressValue(hwnd, state == TaskbarState::Error ? 1000 : ULONGLONG(permille), 1000);
}

void taskbar_attention(SDL_Window* window) {
    HWND hwnd = handle(window);
    if (!hwnd || GetForegroundWindow() == hwnd) return;
    FLASHWINFO info{sizeof info, hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0};
    FlashWindowEx(&info);
}
}
