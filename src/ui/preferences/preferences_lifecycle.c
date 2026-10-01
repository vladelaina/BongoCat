#include "preferences_controls.h"
#include "preferences_gl.h"
#include "preferences_model_cover.h"
#include "preferences_state.h"
#include "ui_animation.h"
#include "bongo_cat/memory.h"
#include "bongo_cat/platform.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdlib.h>

static void release_window(BongoCatPreferences *value) {
    if (!value || !value->window) return;
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
    bongo_cat_preferences_gl_destroy(value);
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
}

void bongo_cat_preferences_show(BongoCatPreferences *value) {
    if (!value) return;
#ifdef __APPLE__
    bongo_cat_preferences_input_monitoring_refresh(value);
#endif
    if (value->visible) {
        bongo_cat_platform_raise_window(value->window);
        bongo_cat_app_request_nearby_model_refresh(value->app);
        return;
    }
    uint64_t requested_ns = SDL_GetTicksNS();
    bool opening = !value->window;
    if (opening && !bongo_cat_preferences_open_window(value)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Preferences failed: %s", SDL_GetError());
        release_window(value);
        return;
    }
    if (value->ui_initialized) bongo_cat_ui_input_reset(&value->ui);
    value->shown_ns = requested_ns;
    value->visible = true;
    bongo_cat_about_refresh(value);
    if (!opening) {
        SDL_StartTextInput(value->window);
        bongo_cat_preferences_live_resize_install(value);
    }
    value->render_dirty = true;
    bongo_cat_preferences_render(value);
    bongo_cat_platform_raise_window(value->window);
    bongo_cat_app_request_nearby_model_refresh(value->app);
}

void bongo_cat_preferences_close(BongoCatPreferences *value) {
    if (!value || !value->window || !value->visible) return;
    bongo_cat_preferences_live_resize_uninstall(value);
    if (bongo_cat_preferences_behavior_dialog_active(value))
        bongo_cat_preferences_behavior_dialog_close(value);
    bongo_cat_preferences_model_rename_finish(value, true);
    bongo_cat_preferences_shortcut_cancel(value);
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
    value->visible = false;
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bool about_gl_ready = SDL_GL_MakeCurrent(value->window, value->gl_context);
    bongo_cat_about_clear(value, about_gl_ready);
    SDL_GL_MakeCurrent(previous_window, previous_context);
    SDL_HideWindow(value->window);
    bongo_cat_preferences_release_idle_window(value);
    bongo_cat_config_store_flush(value->app);
}

void bongo_cat_preferences_release_idle_window(BongoCatPreferences *value) {
    if (!value || !value->window || value->visible) return;
    /* A folder dialog still needs its owner; import completion can queue a
       catalog refresh. Keep the window until both have finished. */
    if (bongo_cat_preferences_import_is_open(value->import_dialog) ||
        bongo_cat_app_model_refresh_busy(value->app) || value->model_loading ||
        value->model_selection_pending) return;
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
