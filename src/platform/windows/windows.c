#include "bongo_cat/platform.h"
#include "windows_borderless.h"
#include "windows_capture.h"
#include "windows_direct_input.h"
#include "windows_input.h"
#include "windows_layered.h"
#include "windows_startup.h"
#ifdef _WIN32
#include <SDL3/SDL.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>
#include <string.h>
#include <windows.h>
static HWND native_window(BongoCatPlatform *platform) {
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(platform->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

static bool SDLCALL windows_message_hook(void *userdata, MSG *message) {
    (void)userdata;
    if (!message || !message->hwnd) return true;
    /* The capture handler filters by its per-window properties. Returning
       false only consumes the refresh/timer messages that it owns. */
    return !bongo_cat_windows_capture_handle_message(message->hwnd,
        message->message, message->wParam);
}

void bongo_cat_platform_configure_preferences_window(SDL_Window *window) {
    if (!window) return;
    HWND handle = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    bool transparent = (SDL_GetWindowFlags(window) &
        SDL_WINDOW_TRANSPARENT) != 0;
    bongo_cat_windows_capture_mark_transparent(handle, transparent);
    if (transparent) {
        bongo_cat_windows_capture_install_transparency_handler(handle);
        bongo_cat_windows_capture_repair_transparency(handle);
    }
}

BongoCatResult bongo_cat_platform_init(BongoCatPlatform *platform, SDL_Window *window,
    BongoCatInputState *input, BongoCatError *error) {
    if (!platform || !window || !input) return BONGO_CAT_ERROR_ARGUMENT;
    memset(platform, 0, sizeof(*platform));
    platform->window = window; platform->input = input;
    platform->window_opacity = 1.0f; platform->presenter = bongo_cat_windows_layered_create();
    if (!platform->presenter) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate the Windows layered presenter");
        return BONGO_CAT_ERROR_MEMORY;
    }
    platform->wake_event_type = SDL_RegisterEvents(1);
    if (platform->wake_event_type == (Uint32)-1) {
        bongo_cat_windows_layered_destroy(platform);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot reserve the Windows input wake event");
        return BONGO_CAT_ERROR_PLATFORM;
    }
    if (!bongo_cat_windows_input_start(platform)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Global input hooks are unavailable; the window will continue without global input");
    }
    HWND hwnd = native_window(platform);
    SetWindowTextW(hwnd, bongo_cat_windows_instance_title());
    bongo_cat_windows_borderless_install(hwnd);
    bool transparent = (SDL_GetWindowFlags(window) &
        SDL_WINDOW_TRANSPARENT) != 0;
    /* Mark transparency before any OBS style transaction. Removing
       WS_EX_TOOLWINDOW may recreate the DWM surface, so the configure path
       must be able to repair it before the window is first shown. */
    bongo_cat_windows_capture_mark_transparent(hwnd, transparent);
    bongo_cat_windows_capture_configure(hwnd);
    SDL_SetWindowsMessageHook(windows_message_hook, NULL);
    if (!SDL_SetWindowResizable(window, true)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Borderless resize is unavailable: %s", SDL_GetError());
    }
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE |
        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    bongo_cat_windows_capture_repair_transparency(hwnd);
    bongo_cat_windows_capture_log(hwnd, "initialized");
    return BONGO_CAT_OK;
}
void bongo_cat_platform_shutdown(BongoCatPlatform *platform) {
    if (!platform) return;
    HWND window = native_window(platform);
    if (window) bongo_cat_windows_borderless_uninstall(window);
    bongo_cat_windows_direct_input_destroy(platform);
    bongo_cat_windows_input_stop(platform);
    bongo_cat_windows_layered_destroy(platform);
    SDL_SetWindowsMessageHook(NULL, NULL);
}

static HWND desktop_anchor(void) {
    HWND program_manager = FindWindowW(L"Progman", NULL);
    if (!program_manager) return NULL;
    HWND worker = NULL;
    while ((worker = FindWindowExW(NULL, worker, L"WorkerW", NULL)) != NULL)
        if (FindWindowExW(worker, NULL, L"SHELLDLL_DefView", NULL))
            return worker;
    return program_manager;
}

static void apply_window_level(HWND window, bool topmost) {
    if (!window) return;
    RECT before = {0};
    bool positioned = GetWindowRect(window, &before) != FALSE;
    HWND owner = topmost ? NULL : desktop_anchor();
    SetWindowLongPtrW(window, GWLP_HWNDPARENT, (LONG_PTR)owner);
    SetWindowPos(window, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
        SWP_FRAMECHANGED);
    if (!topmost)
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (positioned) {
        RECT after = {0};
        if (GetWindowRect(window, &after) &&
            (after.left != before.left || after.top != before.top))
            SetWindowPos(window, NULL, before.left, before.top, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void bongo_cat_platform_set_always_on_top(BongoCatPlatform *platform, bool enabled) {
    if (!platform || !platform->window) return;
    bool applied = SDL_SetWindowAlwaysOnTop(platform->window, enabled);
    HWND window = native_window(platform);
    if (!applied && window)
        SetWindowPos(window, enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    apply_window_level(window, enabled);
    bongo_cat_windows_layered_set_always_on_top(platform, enabled);
    bongo_cat_windows_capture_configure(window);
    bongo_cat_windows_capture_log(window, "window-level");
}
void bongo_cat_platform_begin_drag(BongoCatPlatform *platform,
    BongoCatModalTick modal_tick, void *userdata) {
    HWND hwnd = native_window(platform);
    bongo_cat_windows_begin_drag(hwnd, modal_tick, userdata);
}
bool bongo_cat_platform_dynamic_hit_supported(void) { return true; }
bool bongo_cat_platform_pointer_locked(BongoCatPlatform *platform) {
    if (!platform) return false;
    HWND foreground = GetForegroundWindow();
    DWORD foreground_pid = 0;
    if (!foreground || !GetWindowThreadProcessId(foreground, &foreground_pid) ||
        foreground_pid == GetCurrentProcessId()) return false;
    RECT clip = {0};
    if (!GetClipCursor(&clip)) return false;
    LONG width = clip.right - clip.left;
    LONG height = clip.bottom - clip.top;
    return width >= 0 && width <= 2 && height >= 0 && height <= 2;
}
bool bongo_cat_platform_relative_pointer(BongoCatPlatform *platform,
    double *x, double *y) {
    if (!platform || !x || !y) return false;
    uint64_t now_ms = GetTickCount64();
    if (!platform->relative_pointer) {
        if (platform->relative_pointer_retry_ms > now_ms) return false;
        if (!bongo_cat_windows_direct_input_create(platform,
            native_window(platform))) {
            platform->relative_pointer_retry_ms = now_ms + 5000;
            return false;
        }
        platform->relative_pointer_retry_ms = 0;
    }
    return bongo_cat_windows_direct_input_read(platform, x, y);
}
void bongo_cat_platform_relative_pointer_reset(BongoCatPlatform *platform) {
    bongo_cat_windows_direct_input_reset(platform);
}
void bongo_cat_platform_relative_pointer_release(BongoCatPlatform *platform) {
    if (!platform) return;
    bongo_cat_windows_direct_input_destroy(platform);
    platform->relative_pointer_retry_ms = 0;
}
#endif
