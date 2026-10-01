#include "bongo_cat/audio.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/app.h"
#include "bongo_cat/memory.h"
#include "bongo_cat/memory_policy.h"
#include "bongo_cat/platform.h"
#include "preferences_controls.h"
#include "preferences_model_cover.h"
#include "preferences_state.h"
#include "ui_animation.h"
#include "ui_catime.h"
#include "ui_native_theme.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdlib.h>

int bongo_cat_preferences_resolved_theme(const BongoCatPreferences *value) {
    if (value->app->settings.app.theme == BONGO_CAT_THEME_DARK) return 1;
    if (value->app->settings.app.theme == BONGO_CAT_THEME_LIGHT) return 0;
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
}
void bongo_cat_preferences_apply_theme(BongoCatPreferences *value) {
    int dark = bongo_cat_preferences_resolved_theme(value);
    if (dark == value->style_theme) return;
    value->style_theme = dark;
    bongo_cat_ui_apply_theme(&value->ui.context, dark != 0);
    bongo_cat_ui_native_theme_apply(value->window, dark != 0);
}

void bongo_cat_preferences_page_cache_clear(BongoCatPreferences *value,
    int previous_page, int next_page) {
    if (!value || previous_page == next_page) return;
    bool released = false;
    if (previous_page == 3 && next_page != 3) {
        value->about.qr_open = false;
        bongo_cat_preferences_support_assets_clear(value);
        released = true;
    }
    if (released) bongo_cat_memory_policy_ui_loaded();
}

BongoCatPreferences *bongo_cat_preferences_create(BongoCatApp *app) {
    if (!app) return NULL;
    BongoCatPreferences *value = calloc(1, sizeof(*value));
    if (value) { value->app = app;
        value->import_dialog = bongo_cat_preferences_import_create();
        if (!value->import_dialog) { free(value); return NULL; } }
    if (value && app->smoke_preference_page >= 0)
        value->page = app->smoke_preference_page;
    return value;
}

void bongo_cat_preferences_request_model_import(BongoCatPreferences *value) {
    if (value && !bongo_cat_preferences_import_is_open(value->import_dialog))
        value->import_requested = true; }
bool bongo_cat_preferences_open_model_import(BongoCatPreferences *value,
    SDL_Window *parent) {
    return value && parent && bongo_cat_preferences_import_open(
        value->import_dialog, parent);
}
bool bongo_cat_preferences_visible(const BongoCatPreferences *value) {
    return value && value->window && value->visible;
}
bool bongo_cat_preferences_needs_frame(BongoCatPreferences *value) {
    if (!value) return false;
    uint64_t now = SDL_GetTicksNS();
    if (value->model_load_visual_active &&
        !value->model_load_visual_completion_ns && !value->model_loading &&
        !value->model_selection_pending && now -
        value->model_load_visual_started_ns >=
            BONGO_CAT_MODEL_LOAD_VISUAL_DURATION_NS) {
        value->model_load_visual_active = false;
        value->model_load_progress = 1.0f;
        value->render_dirty = true;
    }
    if (!value->window || !value->visible) return false;
    if (value->about.qr_open && SDL_GetTicks() >= value->about.qr_hide_at)
        value->render_dirty = true;
    if (value->behavior_dialog && value->app) {
        for (size_t i = 0; i < value->app->behaviors.count; ++i) {
            const BongoCatBehaviorEntry *entry = &value->app->behaviors.entries[i];
            bool playing = entry->kind == BONGO_CAT_BEHAVIOR_SOUND &&
                (entry->sound_clear ? bongo_cat_audio_any_playing(value->app->audio) :
                bongo_cat_audio_is_playing(value->app->audio, entry->sound));
            if (value->behavior_audio_playing[i] != playing) {
                value->behavior_audio_playing[i] = playing;
                value->render_dirty = true;
            }
        }
    }
    if (value->render_retry_ns > now) return false;
    bool raster_due = value->pending_raster_scale > 0.0f &&
        value->raster_retry_ns <= now;
    return value->render_dirty || raster_due || value->chrome_dragging; }
void bongo_cat_preferences_input_begin(BongoCatPreferences *value) {
    if (!value || !value->window || !value->visible || value->input_active) return;
    bongo_cat_ui_input_begin(&value->ui);
    value->input_active = true; }
void bongo_cat_preferences_input_end(BongoCatPreferences *value) {
    if (!value || !value->window || !value->input_active) return;
    bongo_cat_ui_input_end(&value->ui);
    value->input_active = false;
}
static Uint32 event_window(const SDL_Event *event) {
    if (event->type >= SDL_EVENT_WINDOW_FIRST && event->type <= SDL_EVENT_WINDOW_LAST)
        return event->window.windowID;
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: return event->key.windowID;
    case SDL_EVENT_TEXT_INPUT: return event->text.windowID;
    case SDL_EVENT_MOUSE_MOTION: return event->motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        return event->button.windowID;
    case SDL_EVENT_MOUSE_WHEEL: return event->wheel.windowID;
    case SDL_EVENT_DROP_BEGIN: case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_COMPLETE: case SDL_EVENT_DROP_POSITION:
        return event->drop.windowID;
    default: return 0;
    }
}

static void enter_model_drop(BongoCatPreferences *value) {
    value->page = 1;
    /* Reveal the import card and cancel any pending scroll animation. */
    value->scroll_current[1] = 0.0f;
    value->scroll_target[1] = 0.0f;
    value->scroll_ready[1] = true;
    value->import_drop_active = true;
    value->render_dirty = true;
}

bool bongo_cat_preferences_chrome_drag_allowed(
    const BongoCatPreferences *value) {
    return value && value->app && !value->native_drag &&
        !bongo_cat_preferences_behavior_dialog_active(value) &&
        !bongo_cat_preferences_remove_dialog_active(value->app);
}

void bongo_cat_preferences_drag_tick(BongoCatPreferences *value) {
    if (!value || !value->window || !value->visible ||
        !value->chrome_dragging) return;
    float pointer_x = 0.0f, pointer_y = 0.0f;
    SDL_MouseButtonFlags buttons = SDL_GetGlobalMouseState(
        &pointer_x, &pointer_y);
    if (!(buttons & SDL_BUTTON_LMASK)) {
        SDL_CaptureMouse(false);
        value->chrome_dragging = false;
        return;
    }
    int current_x = 0, current_y = 0;
    if (!SDL_GetWindowPosition(value->window, &current_x, &current_y)) return;
    int next_x = value->drag_window_x +
        (int)(pointer_x - value->drag_pointer_x);
    int next_y = value->drag_window_y +
        (int)(pointer_y - value->drag_pointer_y);
    if (next_x != current_x || next_y != current_y)
        SDL_SetWindowPosition(value->window, next_x, next_y);
}

static bool chrome_event(BongoCatPreferences *value, const SDL_Event *event) {
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        if (value->about.qr_open) {
            value->about.qr_open = false;
            value->render_dirty = true;
            return true;
        }
        if (bongo_cat_preferences_behavior_dialog_active(value)) {
            bongo_cat_preferences_behavior_dialog_close(value);
            value->render_dirty = true;
            return true;
        }
        if (bongo_cat_preferences_remove_dialog_active(value->app)) {
            bongo_cat_preferences_remove_dialog_close(value->app);
            value->render_dirty = true;
            return true;
        }
        bongo_cat_preferences_close(value);
        return true;
    }
    if (!bongo_cat_preferences_chrome_drag_allowed(value)) {
        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
            event->button.button == SDL_BUTTON_LEFT && value->chrome_dragging) {
            SDL_CaptureMouse(false);
            value->chrome_dragging = false;
            return true;
        }
        return false;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
        event->button.button == SDL_BUTTON_LEFT) {
        int width = 0;
        SDL_GetWindowSize(value->window, &width, NULL);
        float scale = value->ui.layout_scale > 0.0f ? value->ui.layout_scale : 1.0f;
        if (!bongo_cat_ui_title_drag_hit(event->button.x / scale,
            event->button.y / scale, width / scale)) return false;
        SDL_GetWindowPosition(value->window, &value->drag_window_x,
            &value->drag_window_y);
        SDL_GetGlobalMouseState(&value->drag_pointer_x, &value->drag_pointer_y);
        value->chrome_dragging = true;
        if (!SDL_CaptureMouse(true))
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "Mouse capture is unavailable during preferences drag: %s",
                SDL_GetError());
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP &&
        event->button.button == SDL_BUTTON_LEFT && value->chrome_dragging) {
        SDL_CaptureMouse(false);
        value->chrome_dragging = false;
        return true;
    }
    if (event->type != SDL_EVENT_MOUSE_MOTION || !value->chrome_dragging)
        return false;
    bongo_cat_preferences_drag_tick(value);
    return true;
}

bool bongo_cat_preferences_event(BongoCatPreferences *value, const SDL_Event *event) {
    if (!value || !event) return false;
    if (bongo_cat_about_event(value, event)) return true;
    if (bongo_cat_preferences_import_event(value->import_dialog, value->app,
        event)) { value->render_dirty = value->visible;
        return true; }
    if (!value->window) return false;
    if (event->type == SDL_EVENT_SYSTEM_THEME_CHANGED) {
        value->render_dirty = true; return false;
    }
    if (!value->visible) return false;
    if (event_window(event) != SDL_GetWindowID(value->window)) return false;
    if (value->shown_ns && event->common.timestamp &&
        event->common.timestamp < value->shown_ns) return true;
    if (event->type == SDL_EVENT_DROP_BEGIN) {
        enter_model_drop(value);
        return true;
    }
    if (event->type == SDL_EVENT_DROP_POSITION) {
        enter_model_drop(value);
        return true;
    }
    if (event->type == SDL_EVENT_DROP_COMPLETE) {
        value->import_drop_active = false;
        value->render_dirty = true;
        return true;
    }
    if (bongo_cat_preferences_scale_event(value, event)) return true;
#ifdef __APPLE__
    /* The user returns from System Settings through this window. */
    if (event->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        bongo_cat_preferences_input_monitoring_refresh(value);
        value->render_dirty = true;
    }
#endif
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        bongo_cat_ui_input_reset(&value->ui);
        if (value->chrome_dragging) SDL_CaptureMouse(false);
        value->chrome_dragging = false;
        value->import_drop_active = false;
        bongo_cat_pref_controls_reset(&value->ui.context);
    }
    if (bongo_cat_preferences_model_rename_event(value, event)) return true;
    if (bongo_cat_preferences_behavior_rename_event(value, event)) return true;
    if (bongo_cat_preferences_shortcut_event(value, event)) return true;
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        bongo_cat_preferences_close(value);
        return true;
    }
    if (event->type == SDL_EVENT_DROP_FILE) {
        enter_model_drop(value);
        value->import_drop_active = false;
        bongo_cat_preferences_import_path(value->app, value->window, event->drop.data);
        return true;
    }
    if (chrome_event(value, event)) return true;
    if (!value->input_active) bongo_cat_preferences_input_begin(value);
    bongo_cat_ui_event(&value->ui, event);
    value->render_dirty = true;
    return true;
}

void bongo_cat_preferences_invalidate(BongoCatPreferences *value) {
    if (value) value->render_dirty = true; }

void bongo_cat_preferences_models_changed(BongoCatPreferences *value) {
    if (!value) return;
    value->render_dirty = true;
    value->model_directory_watch_known = false;
    value->font_reload_pending = value->ui_initialized;
    /* Catalog refreshes finish between UI frames. Rebuild before drawing the
       new cards so names never appear with missing-glyph placeholders. */
    value->font_reload_defer_once = false;
}
