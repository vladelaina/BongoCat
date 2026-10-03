#include "preferences_controls.h"
#include "preferences_gl.h"
#include "preferences_model_cover.h"
#include "preferences_state.h"
#include "ui_animation.h"
#include "bongo_cat/memory.h"
#include "bongo_cat/memory_policy.h"
#include "bongo_cat/model_memory.h"
#include "bongo_cat/resource_trace.h"
#include "bongo_cat/platform.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

static void hide_window_immediately(SDL_Window *window) {
#ifdef _WIN32
    HWND handle = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    BOOL previous = FALSE, disabled = TRUE;
    bool restore = handle && SUCCEEDED(DwmGetWindowAttribute(handle,
        DWMWA_TRANSITIONS_FORCEDISABLED, &previous, sizeof(previous)));
    if (handle) DwmSetWindowAttribute(handle, DWMWA_TRANSITIONS_FORCEDISABLED,
        &disabled, sizeof(disabled));
#endif
    SDL_HideWindow(window);
#ifdef _WIN32
    if (restore) DwmSetWindowAttribute(handle, DWMWA_TRANSITIONS_FORCEDISABLED,
        &previous, sizeof(previous));
#endif
}

static void release_window(BongoCatPreferences *value) {
    if (!value || !value->window) return;
    bongo_cat_preferences_resource_note(value, "before-release");
    uint64_t release_started = SDL_GetTicksNS();
    bongo_cat_preferences_live_resize_uninstall(value);
    bool context_ready = !value->gl_context ||
        SDL_GL_MakeCurrent(value->window, value->gl_context);
    if (!context_ready) SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
        "Preferences GL cleanup skipped because its context could not be "
        "activated: %s", SDL_GetError());
    if (value->ui_initialized && value->input_active)
        bongo_cat_preferences_input_end(value);
    SDL_StopTextInput(value->window);
    if (context_ready) bongo_cat_preferences_model_cache_clear(value->app);
    else bongo_cat_preferences_model_cache_abandon(value->app);
    if (value->ui_initialized) {
        bongo_cat_pref_controls_reset(&value->ui.context);
        bongo_cat_ui_animations_reset(&value->ui.context);
    }
    bongo_cat_about_clear(value, context_ready);
    if (context_ready) bongo_cat_preferences_assets_clear(value);
    else bongo_cat_preferences_assets_abandon(value);
    if (value->ui_initialized) {
        bongo_cat_ui_cursor_destroy(&value->ui);
        if (context_ready) bongo_cat_ui_destroy(&value->ui);
        else bongo_cat_ui_abandon(&value->ui);
    }
    if (value->chrome_dragging) SDL_CaptureMouse(false);
    /* This window will never swap again. Submit its deletion commands before
       detaching the shared context, without waiting on the GPU here. */
    if (context_ready && value->gl_context) glFlush();
    bool context_destroyed = bongo_cat_preferences_gl_destroy(value);
    SDL_DestroyWindow(value->window);
    value->window = NULL;
    value->visible = false;
    value->transparent_window = false;
    value->ui_initialized = false;
    value->font_reload_pending = false;
    value->font_reload_defer_once = false;
    value->model_load_visual_active = false;
    value->model_load_visual_completion_ns = 0;
    value->smoke_behavior_open_pending = false;
    value->native_drag = false;
    value->chrome_dragging = false;
    value->pending_raster_scale = 0.0f;
    value->raster_retry_ns = 0;
    value->render_retry_ns = 0;
    value->shown_ns = 0;
    SDL_GL_MakeCurrent(value->app->window, value->app->gl_context);
    SDL_GL_SetSwapInterval(1);
    bongo_cat_memory_policy_ui_released();
    bongo_cat_model_memory_ui_state(false, false);
    value->release_wait_flags = 0;
    bongo_cat_resource_trace_end(BONGO_CAT_RESOURCE_SETTINGS, "released",
        "window=%d context=%d gl_cleanup=%d context_destroyed=%d cleanup_ms=%.1f",
        value->window != NULL, value->gl_context != NULL, context_ready, context_destroyed,
        (double)(SDL_GetTicksNS() - release_started) / 1000000.0);
}

void bongo_cat_preferences_show(BongoCatPreferences *value) {
    if (!value) return;
    if (!value->app->models.count) {
        bongo_cat_preferences_page_cache_clear(value, value->page, 1);
        value->page = 1;
        value->scroll_current[1] = 0.0f;
        value->scroll_target[1] = 0.0f;
        value->scroll_ready[1] = true;
        value->render_dirty = true;
    }
#ifdef __APPLE__
    bongo_cat_preferences_input_monitoring_refresh(value);
#endif
    if (value->visible) {
        bongo_cat_platform_raise_window(value->window);
        bongo_cat_app_request_nearby_model_refresh(value->app);
        return;
    }
    uint64_t requested_ns = SDL_GetTicksNS();
    bongo_cat_resource_trace_begin(BONGO_CAT_RESOURCE_SETTINGS, "preferences");
    bool opening = !value->window;
    if (opening && !bongo_cat_preferences_open_window(value)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Preferences failed: %s", SDL_GetError());
        bongo_cat_resource_trace_note(BONGO_CAT_RESOURCE_SETTINGS, "open-failed",
            "error=%s", SDL_GetError());
        release_window(value);
        bongo_cat_resource_trace_end(BONGO_CAT_RESOURCE_SETTINGS, "open-failed", NULL);
        return;
    }
    if (value->ui_initialized) bongo_cat_ui_input_reset(&value->ui);
    value->shown_ns = requested_ns;
    value->release_wait_flags = 0;
    value->visible = true;
    bongo_cat_model_memory_ui_state(true, true);
    bongo_cat_about_refresh(value);
    if (!opening) {
        SDL_StartTextInput(value->window);
        bongo_cat_preferences_live_resize_install(value);
    }
    value->render_dirty = true;
    bongo_cat_preferences_render(value);
    bongo_cat_preferences_resource_note(value, "shown");
    bongo_cat_platform_raise_window(value->window);
    bongo_cat_app_request_nearby_model_refresh(value->app);
}

void bongo_cat_preferences_close(BongoCatPreferences *value) {
    if (!value || !value->window || !value->visible) return;
    bongo_cat_preferences_live_resize_uninstall(value);
    value->visible = false;
    hide_window_immediately(value->window);
    bongo_cat_model_memory_ui_state(true, false);
    bongo_cat_preferences_resource_note(value, "hidden");
    if (bongo_cat_preferences_behavior_dialog_active(value))
        bongo_cat_preferences_behavior_dialog_close(value);
    bongo_cat_preferences_model_rename_finish(value, true);
    bongo_cat_preferences_shortcut_cancel(value);
    bongo_cat_behaviors_clear(value->behavior_catalog); free(value->behavior_catalog);
    value->behavior_catalog = NULL;
    value->behavior_dialog = false;
    value->behavior_dialog_input_armed = false;
    value->behavior_dialog_opened_ns = 0;
    value->behavior_dialog_closing_ns = 0;
    value->import_requested = false;
    value->import_drop_active = false;
    if (value->input_active) bongo_cat_preferences_input_end(value);
    if (value->ui_initialized) bongo_cat_ui_input_reset(&value->ui);
    SDL_StopTextInput(value->window);
    if (value->chrome_dragging) SDL_CaptureMouse(false);
    value->chrome_dragging = false;
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bool about_gl_ready = SDL_GL_MakeCurrent(value->window, value->gl_context);
    bongo_cat_about_clear(value, about_gl_ready);
    SDL_GL_MakeCurrent(previous_window, previous_context);
    bongo_cat_preferences_release_idle_window(value);
    bongo_cat_config_store_flush(value->app);
}

void bongo_cat_preferences_release_idle_window(BongoCatPreferences *value) {
    if (!value || !value->window || value->visible) return;
    /* A folder dialog still needs its owner; import completion can queue a
       catalog refresh. Keep the window until both have finished. */
    unsigned waiting = (bongo_cat_preferences_import_is_open(value->import_dialog) ? 1u : 0u) |
        (bongo_cat_preferences_import_status(value->import_dialog, NULL, NULL, NULL) ? 2u : 0u) |
        (bongo_cat_app_model_refresh_busy(value->app) ? 4u : 0u) |
        (value->model_loading ? 8u : 0u) | (value->model_selection_pending ? 16u : 0u);
    if (waiting) {
        if (value->release_wait_flags != waiting)
            bongo_cat_resource_trace_note(BONGO_CAT_RESOURCE_SETTINGS, "release-deferred",
                "reason_mask=0x%x", waiting);
        value->release_wait_flags = waiting;
        return;
    }
    release_window(value);
}

void bongo_cat_preferences_destroy(BongoCatPreferences *value) {
    if (!value) return;
    bongo_cat_preferences_import_destroy(value->import_dialog);
    value->import_dialog = NULL;
    bongo_cat_preferences_close(value);
    release_window(value);
    bongo_cat_about_clear(value, false);
    bongo_cat_about_shutdown(value);
    free(value);
}
