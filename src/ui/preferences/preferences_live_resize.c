#include "preferences_state.h"
#include "ui_present.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <SDL3/SDL_properties.h>

#define PREFERENCES_RESIZE_SUBCLASS ((UINT_PTR)0xBC53)
#define BONGO_CAT_LIVE_RESIZE_TIMER ((UINT_PTR)0xBC50)
#define BONGO_CAT_LIVE_RESIZE_INTERVAL_MS 16

static HWND native_window(BongoCatPreferences *value) {
    return value && value->window ? (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(value->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL) : NULL;
}

static void render_live(BongoCatPreferences *value) {
    if (!value || !value->window || value->live_resize_rendering) return;
    value->live_resize_rendering = true;
    bool fast = value->live_resize_active;
    value->ui.live_resize_fast = fast;
    if (!value->input_active) bongo_cat_preferences_input_begin(value);
    value->render_dirty = true;
    bongo_cat_preferences_render(value);
    value->ui.live_resize_fast = false;
    if (fast) value->live_resize_layout_frames++;
    value->live_resize_rendering = false;
}

static bool capture_live(BongoCatPreferences *value) {
    if (!value || value->app->loading_model[0] ||
        !SDL_GL_MakeCurrent(value->window, value->gl_context))
        return false;
    bool result = bongo_cat_ui_resize_cache_capture(&value->ui);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    return result;
}

static bool present_live(BongoCatPreferences *value) {
    if (!value || value->app->loading_model[0] ||
        !SDL_GL_MakeCurrent(value->window, value->gl_context))
        return false;
    bool result = bongo_cat_ui_resize_cache_present(&value->ui) &&
        bongo_cat_ui_present(value->window);
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    if (result) bongo_cat_preferences_record_frame(value);
    return result;
}

static void pump_pet(BongoCatPreferences *value) {
    /* The previous model has released its GPU resources during handoff. */
    if (value && !value->app->loading_model[0] && value->live_resize_modal_ready)
        bongo_cat_modal_frame_tick(&value->live_resize_modal_frame);
}

static LRESULT CALLBACK live_resize_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR reference) {
    (void)id;
    BongoCatPreferences *value = (BongoCatPreferences *)reference;
    bool enter = value && message == WM_ENTERSIZEMOVE;
    bool resize = value && value->live_resize_active && message == WM_SIZE;
    bool timer = value && value->live_resize_active && message == WM_TIMER &&
        wparam == BONGO_CAT_LIVE_RESIZE_TIMER;
    if (!enter && !resize && !timer && message != WM_EXITSIZEMOVE)
        return DefSubclassProc(window, message, wparam, lparam);
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    LRESULT result = DefSubclassProc(window, message, wparam, lparam);
    /* A downstream handler may close the window and uninstall this subclass. */
    DWORD_PTR current_reference = 0;
    if (!GetWindowSubclass(window, live_resize_proc, PREFERENCES_RESIZE_SUBCLASS,
            &current_reference) || current_reference != reference) return result;
    if (enter) {
        value->live_resize_active = true;
        value->live_resize_pending = false;
        value->live_resize_timer = SetTimer(window,
            BONGO_CAT_LIVE_RESIZE_TIMER,
            BONGO_CAT_LIVE_RESIZE_INTERVAL_MS, NULL) != 0;
        bongo_cat_modal_frame_init(&value->live_resize_modal_frame,
            value->app);
        value->live_resize_modal_ready = true;
        capture_live(value);
        pump_pet(value);
    }
    if (resize) {
        value->live_resize_pending = true;
        if (!present_live(value) || !value->live_resize_timer) {
            value->live_resize_pending = false;
            render_live(value);
            capture_live(value);
        }
        pump_pet(value);
    }
    if (timer) {
        if (value->live_resize_pending) {
            value->live_resize_pending = false;
            render_live(value);
            capture_live(value);
        }
        pump_pet(value);
    }
    if (value && message == WM_EXITSIZEMOVE) {
        KillTimer(window, BONGO_CAT_LIVE_RESIZE_TIMER);
        value->live_resize_active = false;
        value->live_resize_pending = false;
        value->live_resize_timer = false;
        render_live(value);
        pump_pet(value);
        value->live_resize_modal_ready = false;
        if (!value->app->loading_model[0] &&
            SDL_GL_MakeCurrent(value->window, value->gl_context)) {
            bongo_cat_ui_resize_cache_destroy(&value->ui);
            SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
        }
    }
    if (previous_window && previous_context &&
        SDL_GL_GetCurrentContext() != previous_context)
        SDL_GL_MakeCurrent(previous_window, previous_context);
    return result;
}

void bongo_cat_preferences_live_resize_install(BongoCatPreferences *value) {
    HWND window = native_window(value);
    if (window) SetWindowSubclass(window, live_resize_proc,
        PREFERENCES_RESIZE_SUBCLASS, (DWORD_PTR)value);
}

void bongo_cat_preferences_live_resize_uninstall(BongoCatPreferences *value) {
    if (!value) return;
    HWND window = native_window(value);
    if (window) KillTimer(window, BONGO_CAT_LIVE_RESIZE_TIMER);
    if (window) RemoveWindowSubclass(window, live_resize_proc, PREFERENCES_RESIZE_SUBCLASS);
    value->live_resize_active = false;
    value->live_resize_pending = false;
    value->live_resize_timer = false;
    value->live_resize_modal_ready = false;
    value->ui.live_resize_fast = false;
    value->live_resize_rendering = false;
}
#else
void bongo_cat_preferences_live_resize_install(BongoCatPreferences *value) {
    (void)value;
}
void bongo_cat_preferences_live_resize_uninstall(BongoCatPreferences *value) {
    (void)value;
}
#endif
