#include "runtime.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/log.h"
#include "bongo_cat/model_memory.h"
#include "bongo_cat/resource_trace.h"
#include "model_cover.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/tray.h"

#include <SDL3/SDL_opengl.h>

static void handle_event(BongoCatApp *app, const SDL_Event *event) {
    if (bongo_cat_update_event(app->update, event)) return;
    if (bongo_cat_model_refresh_event(app, event)) return;
    if (bongo_cat_preferences_event(app->preferences, event)) return;
    if (!bongo_cat_window_event(app, event)) app->running = false;
    if (event->type >= SDL_EVENT_GAMEPAD_AXIS_MOTION &&
        event->type <= SDL_EVENT_GAMEPAD_TOUCHPAD_UP) {
        bongo_cat_window_snapshot_end(app);
        bongo_cat_gamepad_event(app, event);
    }
}

static void update_model(BongoCatApp *app, uint64_t now) {
    float elapsed = (float)((now - app->last_frame_ns) / 1000000000.0);
    if (elapsed > 0.25f) elapsed = 0.25f;
    app->last_frame_ns = now;
    if (!app->smoke_freeze_model) bongo_cat_app_step_live2d(app, elapsed);
}

static bool render(BongoCatApp *app, bool present) {
    if (present && app->window_snapshot) {
        bongo_cat_window_snapshot_present(app);
        return app->window_snapshot != NULL;
    }
    uint64_t now = SDL_GetTicksNS();
    if (app->render_retry_ns > now) return false;
    if (!SDL_GL_MakeCurrent(app->window, app->gl_context)) {
        app->dirty = true;
        app->render_retry_ns = now + 1000000000ull;
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
            "Main GL context could not be activated: %s", SDL_GetError());
        return false;
    }
    app->render_retry_ns = 0;
    bongo_cat_window_apply_pending_resize(app);
    bongo_cat_live2d_set_vertical_flip(app->live2d, app->settings.model.vertical_flip);
    bongo_cat_live2d_set_mirror(app->live2d, app->settings.model.mirror);
    bool cover_requested = !present && bongo_cat_model_cover_pending(app);
    bool cover_ready = !cover_requested ||
        bongo_cat_live2d_prepare_cover_capture(app->live2d);
    /* Measure the final pose and allocate its frame before clearing/drawing,
       so newly revealed motion geometry is protected on its first frame. */
    bongo_cat_window_update_model_frame(app);
    bongo_cat_window_apply_pending_resize(app);
    int width, height;
    SDL_GetWindowSizeInPixels(app->window, &width, &height);
    glViewport(0, 0, width, height);
    glEnable(GL_MULTISAMPLE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    bongo_cat_window_clear_background(app);
    int content_x = 0, content_y = 0, content_width = width,
        content_height = height;
    bool content_viewport = bongo_cat_live2d_viewport(app->live2d,
        &content_x, &content_y, &content_width, &content_height) &&
        content_width > 0 && content_height > 0;
    if (content_viewport)
        glViewport(content_x, content_y, content_width, content_height);
    bongo_cat_overlay_set_vertical_flip(app->overlay, app->settings.model.vertical_flip);
    bongo_cat_overlay_draw_background(app->overlay,
        app->settings.model.mirror);
    glViewport(0, 0, width, height);
    bongo_cat_diagnostics_phase("model-draw");
    if (app->loaded_model[0]) bongo_cat_live2d_draw(app->live2d);
    bongo_cat_diagnostics_phase("overlay-draw");
    if (content_viewport)
        glViewport(content_x, content_y, content_width, content_height);
    bongo_cat_overlay_draw_pointer_before_keys(app->overlay);
    if (app->settings.model.mouse_centered && app->pointer_known &&
        !app->model_pointer_anchor_ready)
        bongo_cat_app_apply_mouse_position(app, app->pointer_x,
            app->pointer_y, 0.0f);
    bongo_cat_overlay_draw_keys(app->overlay, app->settings.model.mirror);
    bongo_cat_overlay_draw_effect(app->overlay, app->settings.model.mirror);
    bongo_cat_overlay_draw_pointer_after_keys(app->overlay);
    glViewport(0, 0, width, height);
    bool cover_handled = true;
    if (cover_requested) {
        if (!cover_ready) {
            bongo_cat_model_cover_defer(app, "model-state-preparation");
            cover_handled = false;
        } else if (!bongo_cat_model_cover_capture(app, width, height)) {
            bongo_cat_model_cover_defer(app, "framebuffer-capture");
            cover_handled = false;
        }
    }
    if (cover_requested) app->dirty = true;
    if (!present) {
        app->dirty = true;
        return cover_handled;
    }
    bool reveal_startup = app->startup_visibility_pending &&
        app->session.window.visible;
    bongo_cat_window_mask_corners(app, width, height);
    bongo_cat_diagnostics_phase("frame-readback-and-hit-test");
    bongo_cat_frame_audit(app, width, height);
    bongo_cat_window_capture_pointer_hit(app, true);
    /* Keep the native window hidden while diagnostics/readback finish. The
       reveal is intentionally adjacent to the swap so an uninitialised front
       buffer cannot be displayed as a black startup frame. */
    bool pre_presented = reveal_startup &&
        bongo_cat_platform_present(&app->platform, width, height);
    if (reveal_startup)
        bongo_cat_platform_set_visible(&app->platform, true);
    bool presented = pre_presented ||
        bongo_cat_platform_present(&app->platform, width, height);
    if (!presented) {
        if (reveal_startup) bongo_cat_platform_set_visible(&app->platform, false);
        app->dirty = true;
        app->render_retry_ns = now + 1000000000ull;
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
            "Main frame presentation failed: %s", SDL_GetError());
        return false;
    }
    if (reveal_startup) app->startup_visibility_pending = false;
    bongo_cat_frame_presented_audit(app);
    app->input_diagnostics.presented_frames++;
    bongo_cat_startup_ready(app);
    bongo_cat_memory_policy_frame_presented();
    app->dirty = false;
    bongo_cat_window_sync_click_through(app);
    bongo_cat_window_schedule_hit_check(app);
    return true;
}

void bongo_cat_app_render_now(BongoCatApp *app) {
    if (app && !app->loading_model[0] && app->window && app->session.window.visible &&
        !app->window_minimized)
        render(app, true);
}

bool bongo_cat_app_capture_pending_model_cover(BongoCatApp *app) {
    if (!app || !app->window || !bongo_cat_model_cover_pending(app)) return false;
    if (app->window_snapshot) return false;
    uint64_t now = SDL_GetTicksNS();
    if (!bongo_cat_model_cover_capture_due(app, now)) return false;
    if (app->window_minimized) {
        SDL_Log("Pending model cover deferred while window is minimized: %s",
            bongo_cat_model_cover_pending_path(app));
        bongo_cat_model_cover_defer(app, "window-minimized");
        return false;
    }
    SDL_Log("[runtime] Capturing pending model cover: model=%s loading=%s "
        "visible=%d dirty=%d motions=%zu expression=%d path=%s",
        app->loaded_model,
        app->loading_model[0] ? app->loading_model : "none",
        app->session.window.visible, app->dirty,
        bongo_cat_app_selected_motion_count(app),
        bongo_cat_live2d_expression(app->live2d),
        bongo_cat_model_cover_pending_path(app));
    SDL_Window *previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    bool restore_context = previous_window && previous_context &&
        (previous_window != app->window || previous_context != app->gl_context);
    bool captured = render(app, false);
    if (restore_context &&
        !SDL_GL_MakeCurrent(previous_window, previous_context))
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO,
            "Cannot restore the OpenGL context after model cover capture: %s",
            SDL_GetError());
    return captured;
}

static void take_instance_wake(BongoCatApp *app) {
    if (!bongo_cat_platform_single_instance_take_wake()) return;
    bongo_cat_window_set_visible(app, true);
    if (!app->startup_visibility_pending)
        bongo_cat_platform_raise_window(app->window);
    SDL_Log("Existing instance requested window reveal");
}

static bool take_update_shutdown(BongoCatApp *app) {
    if (!bongo_cat_platform_single_instance_take_update_shutdown())
        return false;
    app->running = false;
    SDL_LogInfo(BONGO_CAT_LOG_UPDATE,
        "Installed update requested application shutdown");
    return true;
}

void bongo_cat_app_loop(BongoCatApp *app) {
    while (app->running) {
        bongo_cat_diagnostics_phase("loop-prepare");
        int wait_ms = bongo_cat_window_wait_timeout(app, SDL_GetTicksNS());
        if (app->secondary_pet && wait_ms > 100) wait_ms = 100;
        bongo_cat_preferences_input_begin(app->preferences);
        SDL_Event event;
        /* An unbounded idle wait is normal; a bounded frame wait should return. */
        bongo_cat_diagnostics_phase(wait_ms >= 0 && wait_ms < 5000 ? "event-wait" : NULL);
        if (bongo_cat_wait_event(&event, wait_ms)) {
            bongo_cat_diagnostics_phase("event-dispatch");
            handle_event(app, &event);
            unsigned queued = 0;
            while (queued++ < 256 && SDL_PollEvent(&event))
                handle_event(app, &event);
        }
        bongo_cat_preferences_input_end(app->preferences);
        bongo_cat_window_resize_update(app, SDL_GetTicksNS());
        /* Recover if capture/focus changes swallowed the release event. */
        if ((app->resize_candidate || app->resize_gesture) &&
            !SDL_HasEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_UP) &&
            !(SDL_GetGlobalMouseState(NULL, NULL) & SDL_BUTTON_RMASK))
            bongo_cat_window_resize_end(app);
        bongo_cat_diagnostics_phase("model-watch-and-refresh");
        uint64_t now = SDL_GetTicksNS();
        bongo_cat_preferences_model_watch(app->preferences, now);
        bongo_cat_model_refresh_update(app);
        take_instance_wake(app);
        if (bongo_cat_platform_single_instance_take_settings()) {
            bongo_cat_preferences_show(app->preferences);
            SDL_Log("Existing instance requested settings window");
        }
        if (take_update_shutdown(app)) continue;
        now = SDL_GetTicksNS();
        bongo_cat_window_update_wheel_animation(app, now);
        bongo_cat_diagnostics_phase("snapshot-and-multi-pet");
        bongo_cat_window_snapshot_update(app, now);
        bongo_cat_multi_pet_update(app, now);
        bongo_cat_memory_policy_poll();
        bongo_cat_model_memory_poll();
        bongo_cat_diagnostics_phase("random-behavior");
        bongo_cat_random_behavior_update(app, now);
        bongo_cat_diagnostics_phase("audio-and-display-recovery");
        bongo_cat_audio_update(app->audio);
        bongo_cat_window_update_display_recovery(app, now);
        bongo_cat_runtime_flow_update(app, now);
        bongo_cat_window_apply_pending_resize(app);
        bongo_cat_app_refresh_texture_resolution(app, true);
        bongo_cat_resource_trace_poll();
        bongo_cat_app_drain_input(app, true);
        if (app->context_menu_requested) {
            app->context_menu_requested = false;
            bongo_cat_diagnostics_phase(NULL); /* Modal menu owns its own loop. */
            bongo_cat_window_show_context_menu(app);
        }
        bongo_cat_diagnostics_phase("hover-and-pointer-hit-test");
        now = SDL_GetTicksNS();
        bongo_cat_app_update_hover(app, now);
        bongo_cat_app_update_hover_fade(app, now);
        bongo_cat_diagnostics_phase("model-update");
        if (bongo_cat_model_frame_due(app, now)) update_model(app, now);
        else if (!app->session.window.visible || app->window_minimized) {
            app->last_frame_ns = now;
            bongo_cat_memory_policy_idle();
        }
        bongo_cat_diagnostics_phase("model-cover-capture");
        bongo_cat_app_capture_pending_model_cover(app);
        bongo_cat_diagnostics_phase("pet-render");
        if (app->session.window.visible && !app->window_minimized && app->dirty)
            render(app, true);
        bongo_cat_diagnostics_phase("preferences-render");
        bongo_cat_preferences_render(app->preferences);
        bongo_cat_diagnostics_phase("model-selection");
        bongo_cat_preferences_process_model_selection(app->preferences);
        if (app->session.window.visible && !app->window_minimized && app->dirty)
            render(app, true);
        bongo_cat_diagnostics_phase("tray-and-config-save");
        bongo_cat_tray_sync(app->tray);
        bongo_cat_window_raise_when_due(app, now);
        bongo_cat_config_store_update(app, now);
        if (app->smoke_deadline_ns && now >= app->smoke_deadline_ns)
            app->running = false;
    }
    bongo_cat_diagnostics_phase("loop-exit");
}
