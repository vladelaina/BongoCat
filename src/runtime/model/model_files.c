#include "runtime.h"
#include "model_import.h"
#include "model_behavior_cache.h"
#include "model_cover.h"
#include "model_geometry.h"
#include "mver/mver_render.h"
#include "mver/mver_config.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/log.h"
#include "bongo_cat/model_memory.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void select_model_state(BongoCatApp *app, const BongoCatModelEntry *entry) {
    bongo_cat_session_remove_model(&app->session, entry->id);
    snprintf(app->session.active_model_id, sizeof(app->session.active_model_id), "%s",
        entry->id);
    app->loaded_mode = entry->mode;
    app->loaded_gamepad_keyboard = entry->mode == BONGO_CAT_MODE_GAMEPAD &&
        (entry->source_format == BONGO_CAT_MODEL_SOURCE_MVER ||
         entry->source_format == BONGO_CAT_MODEL_SOURCE_MVER_PATCH) &&
        bongo_cat_mver_gamepad_keyboard(entry->directory);
    bongo_cat_gamepads_set_enabled(app, entry->mode == BONGO_CAT_MODE_GAMEPAD);
}
static void request_model_frame(BongoCatApp *app, bool reveal) {
    if (!app) return;
    if (app->window) {
        SDL_WindowFlags flags = SDL_GetWindowFlags(app->window);
        bool hidden = (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) != 0;
        if (reveal && (!app->session.window.visible || hidden ||
            app->hover_hidden))
            bongo_cat_window_set_visible(app, true);
        else if (app->session.window.visible)
            bongo_cat_window_mark_hit_dirty(app);
    }
    bongo_cat_window_mark_hit_dirty(app);
    app->dirty = true;
}

static void model_runtime_stage(BongoCatApp *app, const char *stage,
    const BongoCatModelEntry *entry) {
    char state[BONGO_CAT_ID_CAP + 32];
    const char *id = entry ? entry->id : "unknown";
    snprintf(state, sizeof(state), "model-load:%s:%s", stage, id);
    bongo_cat_runtime_stage(app, state);
    SDL_Log("[runtime] Model load %s: id=%s", stage, id);
}

static void model_running_stage(BongoCatApp *app, const char *model_id) {
    char state[BONGO_CAT_ID_CAP + 16];
    snprintf(state, sizeof(state), "running:model:%s",
        model_id && model_id[0] ? model_id : "none");
    bongo_cat_runtime_stage(app, state);
}

static void model_progress_runtime_stage(BongoCatApp *app, float progress) {
    static const char *names[] = {"", "model-core", "texture-loading",
        "finalizing"};
    unsigned stage = progress >= .95f ? 3 : progress >= .50f ? 2 : 1;
    if (!app) return;
    /* Real progress is a watchdog heartbeat, even within the same stage.
       This updates memory only; the on-disk stage still changes sparingly. */
    bongo_cat_diagnostics_phase(names[stage]);
    if (stage == app->model_load_runtime_stage) return;
    app->model_load_runtime_stage = stage;
    char state[BONGO_CAT_ID_CAP + 40];
    snprintf(state, sizeof(state), "model-load:%s:%s", names[stage],
        app->loading_model[0] ? app->loading_model : "unknown");
    bongo_cat_runtime_stage(app, state);
}

static void model_load_progress(void *userdata, float progress) {
    BongoCatApp *app = userdata;
    bongo_cat_model_memory_sample();
    model_progress_runtime_stage(app, progress);
    /* Keep native window procedures responsive while the main loop is
       synchronously loading Cubism and OpenGL resources. */
    SDL_PumpEvents();
    if (app && app->preferences)
        bongo_cat_preferences_model_load_progress(app->preferences, progress);
    /* The main loop is blocked during loading, so advance the visible model here. */
    if (!app || !app->loaded_model[0] || !app->model_load_last_frame_ns ||
        !app->live2d)
        return;
    uint64_t now = SDL_GetTicksNS();
    if (progress < .98f && now - app->model_load_last_frame_ns < 16000000ull)
        return;
    uint64_t previous = app->model_load_last_frame_ns;
    app->model_load_last_frame_ns = now;
    float elapsed = (float)((now - previous) / 1000000000.0);
    bongo_cat_app_drain_input(app, false);
    now = SDL_GetTicksNS();
    bongo_cat_app_update_hover(app, now);
    bongo_cat_app_update_hover_fade(app, now);
    if (!app->smoke_freeze_model && elapsed > 0.0f)
        bongo_cat_app_step_live2d(app, elapsed);
    app->last_frame_ns = now;
    bongo_cat_app_render_now(app);
    if (progress >= 1.0f) bongo_cat_app_log_input(app, true);
}

static void commit_model(BongoCatApp *app,
    const BongoCatModelEntry *entry, bool force_refresh, bool reveal) {
    bool changed = strcmp(app->session.active_model_id, entry->id) != 0 ||
        app->loaded_mode != entry->mode;
    if (!changed && !force_refresh) return;
    select_model_state(app, entry);
    app->model_selection_serial++;
    request_model_frame(app, reveal);
}

static bool select_model_with_error(BongoCatApp *app, const char *id,
    BongoCatError *error, bool force_reload) {
    BongoCatError local = {0};
    BongoCatError *failure = error ? error : &local;
    *failure = (BongoCatError){0};
    if (!app || !app->live2d || !id) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_ARGUMENT,
            "Cannot select a model without an active renderer and model id");
        return false;
    }
    const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models, id);
    if (!entry) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_ARGUMENT,
            "Model is not installed: %s", id);
        return false;
    }
    bongo_cat_window_snapshot_end(app);
    if (!force_reload && app->loaded_model[0] &&
        strcmp(app->loaded_model, entry->id) == 0) {
        commit_model(app, entry, false, false);
        return true;
    }
    bongo_cat_app_capture_behavior_state(app);
    bongo_cat_app_log_input(app, true);
    bool replacing_model = app->loaded_model[0] != '\0';
    if (replacing_model)
        bongo_cat_model_cover_capture_before_switch(app);
    BongoCatError optional = {0};
    if (!bongo_cat_mver_shortcuts_load(app, entry, &optional))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
    BongoCatBehaviorCatalog *behaviors = calloc(1, sizeof(*behaviors));
    if (!behaviors) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model behavior state");
        return false;
    }
    bool behavior_catalog_valid = bongo_cat_model_behavior_cache_matches(
        app, entry);
    if (behavior_catalog_valid) {
        if (!bongo_cat_behaviors_copy(behaviors, app->behavior_cache, failure)) {
            free(behaviors);
            return false;
        }
    } else {
        behavior_catalog_valid = bongo_cat_behaviors_load(
            behaviors, entry, &optional) == BONGO_CAT_OK;
        if (!behavior_catalog_valid)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
    }
    BongoCatLive2DRenderOptions render_options = {0};
    bool mver = entry->source_format == BONGO_CAT_MODEL_SOURCE_MVER ||
        entry->source_format == BONGO_CAT_MODEL_SOURCE_MVER_PATCH;
    char config_path[BONGO_CAT_PATH_CAP] = {0};
    /* Authored canvas/layout belongs to the package. User window preferences
       remain global and are never loaded from or saved to this file. */
    bool adapted_profile = mver
        ? bongo_cat_mver_config_find(entry->directory, config_path, sizeof(config_path)) &&
            bongo_cat_mver_render_read(config_path, &render_options)
        : bongo_cat_import_render_options(entry->adapter_directory, &render_options);
    if (mver && !adapted_profile) {
        bongo_cat_behaviors_clear(behaviors); free(behaviors);
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_FORMAT,
            "Cannot read Mver rendering configuration: %s", entry->directory);
        return false;
    }
    if (adapted_profile) SDL_Log("Imported runtime profile: projection=%.4f "
        "force_mouse=%d left_handed=%d pointer_bounds=%d",
        render_options.projection_scale, render_options.mouse_force_move,
        render_options.pointer_left_handed, render_options.custom_pointer_bounds);
    BongoCatLive2DTextureOptions texture_options = {
        .dynamic_resolution = app->settings.model.dynamic_texture_resolution,
        .render_quality_percent = app->settings.model.render_quality_percent,
        .display_size = bongo_cat_model_texture_display_size,
        .display_size_userdata = app};
    BongoCatModelContentAnchor content_anchor =
        bongo_cat_model_content_anchor(app);
    int pixel_width = app->session.window.width, pixel_height = app->session.window.height;
    if (app->window) SDL_GetWindowSizeInPixels(app->window, &pixel_width, &pixel_height);
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bool restore_context = previous_window != app->window ||
        previous_context != app->gl_context;
    if (restore_context && !SDL_GL_MakeCurrent(app->window, app->gl_context)) {
        bongo_cat_error_set(failure, BONGO_CAT_ERROR_PLATFORM,
            "Cannot activate the main OpenGL context: %s", SDL_GetError());
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", failure->message);
        bongo_cat_behaviors_clear(behaviors); free(behaviors);
        return false;
    }
    char previous_model[BONGO_CAT_ID_CAP];
    snprintf(previous_model, sizeof(previous_model), "%s",
        app->loaded_model[0] ? app->loaded_model : "none");
    uint64_t load_started_ns = SDL_GetTicksNS();
    bongo_cat_model_memory_begin(previous_model, entry->id,
        texture_options.dynamic_resolution, pixel_width, pixel_height);
    GLenum initial_gl_error = glGetError();
    SDL_Log("[runtime] Model switch transaction: stage=begin previous=%s next=%s "
        "main_window=%p main_context=%p current_window=%p current_context=%p "
        "gl_error=0x%x", previous_model, entry->id, (void *)app->window,
        (void *)app->gl_context, (void *)SDL_GL_GetCurrentWindow(),
        (void *)SDL_GL_GetCurrentContext(), (unsigned)initial_gl_error);
    bongo_cat_live2d_reshape(app->live2d, pixel_width, pixel_height);
    snprintf(app->loading_model, sizeof(app->loading_model), "%s", entry->id);
    app->model_load_runtime_stage = 0;
    model_runtime_stage(app, "started", entry);
    /* The loader releases the old render resources before decoding the new
       model. Keep the already-present front buffer on screen while that
       happens instead of redrawing into an intentionally resource-less model. */
    app->model_load_last_frame_ns = 0;
    const char *previous_phase = bongo_cat_diagnostics_phase("model-load");
    BongoCatResult load_result = bongo_cat_live2d_load_ex(app->live2d,
        entry->directory, entry->setting_file, entry->preset, &render_options,
        &texture_options,
        model_load_progress, app, failure);
    bongo_cat_diagnostics_phase(previous_phase);
    app->model_load_last_frame_ns = 0;
    if (replacing_model) app->last_frame_ns = SDL_GetTicksNS();
    if (load_result != BONGO_CAT_OK) {
        bongo_cat_model_memory_log("load-error", "error=%s", failure->message);
        bongo_cat_model_memory_complete(false, pixel_width, pixel_height);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Model switch transaction: "
            "stage=failed previous=%s next=%s elapsed_ms=%.2f gl_error=0x%x "
            "error=%s", previous_model, entry->id,
            (double)(SDL_GetTicksNS() - load_started_ns) / 1000000.0,
            (unsigned)glGetError(), failure->message);
        if (restore_context && previous_window && previous_context &&
            !SDL_GL_MakeCurrent(previous_window, previous_context))
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "Cannot restore the previous OpenGL context: %s", SDL_GetError());
        bongo_cat_behaviors_clear(behaviors); free(behaviors);
        if (!bongo_cat_live2d_ready(app->live2d)) app->loaded_model[0] = '\0';
        request_model_frame(app, false);
        app->loading_model[0] = '\0';
        app->model_load_runtime_stage = 0;
        model_running_stage(app, app->loaded_model);
        return false;
    }
    model_runtime_stage(app, "committing", entry);
    BongoCatParameterRange pointer_x, pointer_y;
    bool model_pointer =
        bongo_cat_live2d_parameter(app->live2d, "ParamMouseX", &pointer_x) &&
        bongo_cat_live2d_parameter(app->live2d, "ParamMouseY", &pointer_y) &&
        pointer_x.maximum > pointer_x.minimum &&
        pointer_y.maximum > pointer_y.minimum;
    app->model_render_options = render_options;
    app->frame_geometry_retry_ns = 0;
    bongo_cat_app_reset_pointer_tracking(app);
    bongo_cat_live2d_set_render_options(app->live2d, &render_options);
    if (app->loaded_model[0] && app->behavior_catalog_valid) {
        const BongoCatModelEntry *previous_entry = bongo_cat_models_find(
            &app->models, app->loaded_model);
        bongo_cat_model_behavior_cache_store(app, previous_entry);
    } else if (app->loaded_model[0]) {
        app->behavior_cache_valid = false;
    }
    bongo_cat_behaviors_move(&app->behaviors, behaviors);
    app->behavior_catalog_valid = behavior_catalog_valid;
    bongo_cat_behaviors_clear(behaviors); free(behaviors);
    optional = (BongoCatError){0};
    if (bongo_cat_overlay_load(app->overlay, entry->adapter_directory,
        model_pointer, &render_options, &optional) != BONGO_CAT_OK) {
        if (optional.message[0])
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", optional.message);
        bongo_cat_overlay_clear(app->overlay);
    }
    snprintf(app->loaded_model, sizeof(app->loaded_model), "%s", entry->id);
    size_t saved_behaviors = bongo_cat_app_saved_behavior_count(app, entry->id);
    bongo_cat_app_restore_behavior_state(app, entry->id);
    SDL_Log("[runtime] Behavior restore summary: model=%s saved=%zu "
        "motions=%zu expression=%d", entry->id, saved_behaviors,
        bongo_cat_app_selected_motion_count(app),
        bongo_cat_live2d_expression(app->live2d));
    bongo_cat_model_cover_schedule(app, entry);
    bongo_cat_random_behavior_reset(app);
    app->pointer_known = false;
    bool geometry_changed = bongo_cat_model_apply_aspect(app, &render_options,
        &content_anchor, replacing_model);
    if (app->window) {
        if (geometry_changed) SDL_SyncWindow(app->window);
        SDL_GetWindowSizeInPixels(app->window, &pixel_width, &pixel_height);
    }
    bongo_cat_live2d_resize(app->live2d, pixel_width, pixel_height);
    bongo_cat_model_memory_log("window-ready", "window=%dx%d",
        pixel_width, pixel_height);
    bongo_cat_audio_reset(app->audio);
    memset(&app->sound_shortcut_state, 0, sizeof(app->sound_shortcut_state));
    bongo_cat_app_reset_sound_bindings(app);
    commit_model(app, entry, true, replacing_model);
    bongo_cat_preferences_invalidate(app->preferences);
    bongo_cat_app_reapply_input(app);
    bongo_cat_app_apply_mouse(app);
    SDL_Log("[runtime] Model cover load phase: id=%s phase=%s",
        entry->id, replacing_model ? "switch" : "startup");
    /* Finish the handoff before the independent readback so startup and model
       switches both capture the restored state, never the initial state. */
    app->loading_model[0] = '\0';
    app->model_load_runtime_stage = 0;
    app->input_diagnostics.last_gamepad[0] = '\0';
    app->input_diagnostics.last_gamepad_value = 0.0f;
    app->input_diagnostics.last_visual_action[0] = '\0';
    SDL_LogInfo(BONGO_CAT_LOG_INPUT,
        "[input] model-loaded diagnostic_version=3 model=%s mode=%s "
        "source=%d preset=%d managed=%d keyboard_simulation=%d mver_input_mode=%d "
        "adapter_schema=%d adapter_generator=%d active_gamepad=%u "
        "saved_behaviors=%zu motions=%zu expression=%d "
        "directory=%s setting=%s adapter=%s config=%s",
        entry->id, bongo_cat_mode_name(app->loaded_mode), (int)entry->source_format,
        entry->preset, entry->managed, app->loaded_gamepad_keyboard,
        mver && entry->mode == BONGO_CAT_MODE_GAMEPAD
            ? bongo_cat_mver_gamepad_input_mode(entry->directory) : -1,
        entry->adapter_schema, entry->adapter_generator, (unsigned)app->active_gamepad,
        saved_behaviors, bongo_cat_app_selected_motion_count(app),
        bongo_cat_live2d_expression(app->live2d), entry->directory,
        entry->setting_file, entry->adapter_directory, config_path[0] ? config_path : "none");
    bongo_cat_app_log_input(app, true);
    bongo_cat_app_capture_pending_model_cover(app);
    /* Do not leave the previous frame cropped during the UI completion pass. */
    if (replacing_model) bongo_cat_app_render_now(app);
    if (restore_context && previous_window && previous_context &&
        !SDL_GL_MakeCurrent(previous_window, previous_context))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot restore the previous OpenGL context: %s", SDL_GetError());
    bongo_cat_memory_policy_model_loaded();
    bongo_cat_model_memory_complete(true, pixel_width, pixel_height);
    SDL_Log("[runtime] Model switch transaction: stage=complete previous=%s "
        "next=%s elapsed_ms=%.2f current_window=%p current_context=%p "
        "gl_error=0x%x", previous_model, entry->id,
        (double)(SDL_GetTicksNS() - load_started_ns) / 1000000.0,
        (void *)SDL_GL_GetCurrentWindow(), (void *)SDL_GL_GetCurrentContext(),
        (unsigned)glGetError());
    model_runtime_stage(app, "completed", entry);
    model_running_stage(app, entry->id);
    return true;
}

bool bongo_cat_app_select_model(BongoCatApp *app, const char *id) {
    return select_model_with_error(app, id, NULL, false);
}

bool bongo_cat_app_select_model_with_error(BongoCatApp *app,
    const char *id, BongoCatError *error) {
    return select_model_with_error(app, id, error, false);
}

bool bongo_cat_app_reload_model_with_error(BongoCatApp *app,
    BongoCatError *error) {
    if (!app || !app->loaded_model[0]) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Cannot reload without an active model");
        return false;
    }
    char id[BONGO_CAT_ID_CAP];
    snprintf(id, sizeof(id), "%s", app->loaded_model);
    return select_model_with_error(app, id, error, true);
}
