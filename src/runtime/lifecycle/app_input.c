#include "runtime.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/shortcut.h"
#include "bongo_cat/log.h"
#include "bongo_cat/overlay.h"

#include <string.h>

static float input_parameter(BongoCatApp *app, const char *id) {
    BongoCatParameterRange range;
    return bongo_cat_live2d_parameter(app->live2d, id, &range) ? range.value : -1.0f;
}

void bongo_cat_app_log_input(BongoCatApp *app, bool flush) {
    if (!app || !app->input_diagnostics.pending) return;
    uint64_t now = SDL_GetTicks();
    /* Input callbacks only update counters. No per-key messages or idle timer
       output; flush the final partial interval at a model switch/shutdown. */
    if (!flush && now - app->input_diagnostics.log_ms < 10000) return;
    app->input_diagnostics.log_ms = now;
    app->input_diagnostics.pending = false;
    const BongoCatModelEntry *model = bongo_cat_models_find(&app->models, app->loaded_model);
    int x = 0, y = 0;
    bool position_known = app->window && SDL_GetWindowPosition(app->window, &x, &y);
    SDL_DisplayID display = app->window ? SDL_GetDisplayForWindow(app->window) : 0;
    SDL_Rect bounds = {0};
    bool display_known = display && SDL_GetDisplayBounds(display, &bounds);
    SDL_LogInfo(BONGO_CAT_LOG_INPUT,
        "[input] consumer ticks_ms=%llu model=%s mode=%s catalog_mode=%s "
        "keyboard_simulation=%d flush=%d totals=process "
        "drained_keys=%llu ignored_keys=%llu "
        "mapped_keys=%llu unmapped_keys=%llu no_model_keys=%llu frames=%llu "
        "received_keys=%llu mode_blocked_keys=%llu keyboard_overlays=%llu "
        "mouse_buttons=%llu gamepad_buttons=%llu gamepad_axes=%llu "
        "stick_deadzone_events=%llu "
        "ignored_gamepad=%llu gamepad_overlays=%llu replays=%llu replay_blocked_keys=%llu "
        "visual_actions=%llu "
        "model_ready=%d visible=%d minimized=%d dirty=%d "
        "position_known=%d pet_position=%d,%d display_known=%d "
        "pet_display=%u pet_primary=%d pet_bounds=%d,%d,%d,%d",
        (unsigned long long)now,
        app->loaded_model[0] ? app->loaded_model : "none",
        bongo_cat_mode_name(app->loaded_mode),
        model ? bongo_cat_mode_name(model->mode) : "missing",
        app->loaded_gamepad_keyboard, flush,
        (unsigned long long)app->input_diagnostics.drained_keys,
        (unsigned long long)app->input_diagnostics.ignored_keys,
        (unsigned long long)app->input_diagnostics.mapped_keys,
        (unsigned long long)app->input_diagnostics.unmapped_keys,
        (unsigned long long)app->input_diagnostics.no_model_keys,
        (unsigned long long)app->input_diagnostics.presented_frames,
        (unsigned long long)app->input_diagnostics.received_keys,
        (unsigned long long)app->input_diagnostics.mode_blocked_keys,
        (unsigned long long)app->input_diagnostics.keyboard_overlays,
        (unsigned long long)app->input_diagnostics.mouse_buttons,
        (unsigned long long)app->input_diagnostics.gamepad_buttons,
        (unsigned long long)app->input_diagnostics.gamepad_axes,
        (unsigned long long)app->input_diagnostics.stick_deadzone_events,
        (unsigned long long)app->input_diagnostics.ignored_gamepad,
        (unsigned long long)app->input_diagnostics.gamepad_overlays,
        (unsigned long long)app->input_diagnostics.replays,
        (unsigned long long)app->input_diagnostics.replay_blocked_keys,
        (unsigned long long)app->input_diagnostics.visual_actions,
        app->live2d != NULL, app->session.window.visible, app->window_minimized,
        app->dirty, position_known, x, y, display_known, (unsigned)display,
        display && display == SDL_GetPrimaryDisplay(),
        bounds.x, bounds.y, bounds.w, bounds.h);
    if (app->loaded_mode == BONGO_CAT_MODE_GAMEPAD ||
        (model && model->mode == BONGO_CAT_MODE_GAMEPAD)) {
        BongoCatOverlayInputDiagnostics overlay = bongo_cat_overlay_input_diagnostics(app->overlay);
        SDL_LogInfo(BONGO_CAT_LOG_INPUT,
            "[input] hand-state model=%s four_hands=%d stick_deadzone=%.3f hands_seen=%u overlay_hands=%u "
            "model_hands=%.3f,%.3f stick_hands=%.3f,%.3f "
            "sticks=%.3f,%.3f,%.3f,%.3f active_gamepad=%u last_gamepad=%s:%.3f "
            "held_inputs=%zu last_visual_action=%s expression=%d "
            "overlay_directory=%s adapter_match=%d last_overlay=%s effect=%s texture_failures=%llu",
            app->loaded_model, app->settings.model.gamepad_four_hands,
            BONGO_CAT_GAMEPAD_STICK_DEADZONE,
            app->input_diagnostics.hands_seen, overlay.active_hands,
            input_parameter(app, "CatParamLeftHandDown"),
            input_parameter(app, "CatParamRightHandDown"),
            input_parameter(app, "CatParamStickShowLeftHand"),
            input_parameter(app, "CatParamStickShowRightHand"),
            app->left_stick_x, app->left_stick_y, app->right_stick_x, app->right_stick_y,
            (unsigned)app->active_gamepad,
            app->input_diagnostics.last_gamepad[0] ? app->input_diagnostics.last_gamepad : "none",
            app->input_diagnostics.last_gamepad_value, app->active_input_count,
            app->input_diagnostics.last_visual_action[0] ? app->input_diagnostics.last_visual_action : "none",
            bongo_cat_live2d_expression(app->live2d), overlay.directory,
            model && !strcmp(model->adapter_directory, overlay.directory),
            overlay.last_path[0] ? overlay.last_path : "none",
            overlay.effect_path[0] ? overlay.effect_path : "none",
            (unsigned long long)overlay.texture_failures);
    }
    app->input_diagnostics.hands_seen = 0;
}

static void suppress_shortcut(BongoCatApp *app, const BongoCatInputEvent *event) {
    bongo_cat_shortcut_update(&app->shortcut_state, event);
    if (!bongo_cat_sound_shortcut_update(&app->sound_shortcut_state, event)) return;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        const BongoCatBehaviorShortcut *binding = bongo_cat_app_behavior_binding(app, entry->id);
        bool down = binding && !binding->shortcut_disabled &&
            bongo_cat_sound_shortcut_down(&app->sound_shortcut_state, binding->shortcut);
        if (!down && entry->shortcut_active && entry->momentary &&
            entry->kind == BONGO_CAT_BEHAVIOR_EFFECT)
            bongo_cat_overlay_effect(app->overlay, NULL);
        /* Consume held chords without running them after a modal dialog closes. */
        entry->shortcut_active = down;
    }
}

void bongo_cat_app_drain_input(BongoCatApp *app, bool allow_shortcuts) {
    BongoCatInputEvent event;
    while (bongo_cat_input_pop(&app->input, &event)) {
        bool keyboard = event.kind == BONGO_CAT_INPUT_KEY_DOWN ||
            event.kind == BONGO_CAT_INPUT_KEY_UP;
        if (keyboard) app->input_diagnostics.drained_keys++;
        if (app->smoke_ignore_global_input) {
            if (keyboard) app->input_diagnostics.ignored_keys++;
            continue;
        }
        if (strcmp(event.name, "CapsLock") == 0)
            bongo_cat_input_schedule_release(&app->input, &event, 100);
        if (keyboard || event.kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON ||
            event.kind == BONGO_CAT_INPUT_GAMEPAD_AXIS ||
            (!app->window_drag_active && (event.kind == BONGO_CAT_INPUT_MOUSE_DOWN ||
                event.kind == BONGO_CAT_INPUT_MOUSE_UP)))
            bongo_cat_window_snapshot_end(app);
        if (allow_shortcuts &&
            !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        else {
            /* Suppress actions, not key transitions: releases may arrive
               while a shortcut is being recorded or a modal menu is open. */
            suppress_shortcut(app, &event);
        }
        bongo_cat_app_apply_input(app, &event);
    }
    uint64_t now = SDL_GetTicks();
    while (bongo_cat_input_take_scheduled_release(&app->input, now, &event)) {
        if (allow_shortcuts &&
            !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        else suppress_shortcut(app, &event);
        bongo_cat_app_apply_input(app, &event);
    }
    if (!app->smoke_ignore_global_input) bongo_cat_app_apply_mouse(app);
    bongo_cat_app_log_input(app, false);
}
