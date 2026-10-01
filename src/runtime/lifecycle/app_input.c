#include "runtime.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/shortcut.h"
#include "bongo_cat/log.h"

#include <string.h>

static void log_input(BongoCatApp *app, uint64_t now) {
    if (now - app->input_diagnostics.log_ms < 10000) return;
    app->input_diagnostics.log_ms = now;
    int x = 0, y = 0;
    bool position_known = app->window && SDL_GetWindowPosition(app->window, &x, &y);
    SDL_DisplayID display = app->window ? SDL_GetDisplayForWindow(app->window) : 0;
    SDL_Rect bounds = {0};
    bool display_known = display && SDL_GetDisplayBounds(display, &bounds);
    SDL_LogInfo(BONGO_CAT_LOG_INPUT,
        "[input] consumer ticks_ms=%llu drained_keys=%llu ignored_keys=%llu "
        "mapped_keys=%llu unmapped_keys=%llu no_model_keys=%llu frames=%llu "
        "model_ready=%d visible=%d minimized=%d dirty=%d "
        "position_known=%d pet_position=%d,%d display_known=%d "
        "pet_display=%u pet_primary=%d pet_bounds=%d,%d,%d,%d",
        (unsigned long long)now,
        (unsigned long long)app->input_diagnostics.drained_keys,
        (unsigned long long)app->input_diagnostics.ignored_keys,
        (unsigned long long)app->input_diagnostics.mapped_keys,
        (unsigned long long)app->input_diagnostics.unmapped_keys,
        (unsigned long long)app->input_diagnostics.no_model_keys,
        (unsigned long long)app->input_diagnostics.presented_frames,
        app->live2d != NULL, app->session.window.visible, app->window_minimized,
        app->dirty, position_known, x, y, display_known, (unsigned)display,
        display && display == SDL_GetPrimaryDisplay(),
        bounds.x, bounds.y, bounds.w, bounds.h);
}

void bongo_cat_app_drain_input(BongoCatApp *app, bool allow_shortcuts) {
    if (app && app->secondary_pet) allow_shortcuts = false;
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
            bongo_cat_shortcut_update(&app->shortcut_state, &event);
            if (app->sound_shortcut_state.count) {
                app->sound_shortcut_state.count = 0;
                memset(app->sound_shortcut_active, 0, sizeof(app->sound_shortcut_active));
            }
        }
        bongo_cat_app_apply_input(app, &event);
    }
    uint64_t now = SDL_GetTicks();
    while (bongo_cat_input_take_scheduled_release(&app->input, now, &event)) {
        if (allow_shortcuts &&
            !bongo_cat_preferences_shortcuts_blocked(app->preferences))
            bongo_cat_app_shortcuts(app, &event);
        else bongo_cat_shortcut_update(&app->shortcut_state, &event);
        bongo_cat_app_apply_input(app, &event);
    }
    if (!app->smoke_ignore_global_input) bongo_cat_app_apply_mouse(app);
    log_input(app, now);
}
