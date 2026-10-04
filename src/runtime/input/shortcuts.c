#include "runtime.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/shortcut.h"

#include <stdio.h>
#include <string.h>

static void toggle_pet_visibility(BongoCatApp *app) {
    bongo_cat_window_set_visible(app, !app->session.window.visible);
}

static bool hidden_toggle_has_visible_binding(BongoCatApp *app,
    const BongoCatBehaviorEntry *behavior, const char *shortcut) {
    if (!app || !behavior || !shortcut || !shortcut[0] ||
        behavior->kind != BONGO_CAT_BEHAVIOR_MOTION ||
        bongo_cat_live2d_motion_visible(app->live2d,
            behavior->group, behavior->index)) return false;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *candidate = &app->behaviors.entries[i];
        if (candidate == behavior ||
            candidate->kind != BONGO_CAT_BEHAVIOR_MOTION ||
            !bongo_cat_live2d_motion_visible(app->live2d,
                candidate->group, candidate->index) ||
            !bongo_cat_live2d_motion_same_toggle(app->live2d,
                behavior->group, behavior->index,
                candidate->group, candidate->index)) continue;
        const BongoCatBehaviorShortcut *binding =
            bongo_cat_app_behavior_binding(app, candidate->id);
        if (binding && !binding->shortcut_disabled &&
            bongo_cat_shortcut_equal(binding->shortcut, shortcut)) return true;
    }
    return false;
}

static bool behavior_shortcut(BongoCatApp *app, const BongoCatInputEvent *event,
    bool handled, bool sound_edge) {
    const BongoCatModelEntry *model = bongo_cat_models_find(&app->models, app->loaded_model);
    bool mver = model && (model->source_format == BONGO_CAT_MODEL_SOURCE_MVER ||
        model->source_format == BONGO_CAT_MODEL_SOURCE_MVER_PATCH);
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        BongoCatBehaviorEntry *behavior = &app->behaviors.entries[i];
        if (behavior->kind == BONGO_CAT_BEHAVIOR_SOUND) continue;
        const BongoCatBehaviorShortcut *shortcut = bongo_cat_app_behavior_binding(app, behavior->id);
        if (!shortcut || shortcut->shortcut_disabled || !shortcut->shortcut[0]) {
            if (behavior->shortcut_active && behavior->momentary &&
                behavior->kind == BONGO_CAT_BEHAVIOR_EFFECT)
                handled = bongo_cat_overlay_effect(app->overlay, NULL) || handled;
            behavior->shortcut_active = false;
            continue;
        }
        if (mver) {
            /* The held-key state also supports Mver's multi-primary chords.
               Trigger once when the whole chord becomes held, in either order. */
            if (!sound_edge) continue;
            bool down = bongo_cat_sound_shortcut_down(&app->sound_shortcut_state,
                shortcut->shortcut);
            bool pressed = down && !behavior->shortcut_active;
            bool released = !down && behavior->shortcut_active;
            behavior->shortcut_active = down;
            if (released && behavior->momentary &&
                    behavior->kind == BONGO_CAT_BEHAVIOR_EFFECT)
                handled = bongo_cat_overlay_effect(app->overlay, NULL) || handled;
            if (pressed && !hidden_toggle_has_visible_binding(app, behavior,
                    shortcut->shortcut))
                handled = bongo_cat_app_run_behavior(app, behavior) || handled;
            continue;
        }
        if (behavior->shortcut_active &&
            bongo_cat_shortcut_release_matches(event, shortcut->shortcut) &&
            (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON ||
             !bongo_cat_sound_shortcut_down(&app->shortcut_state.held, shortcut->shortcut))) {
            behavior->shortcut_active = false;
            if (behavior->momentary && behavior->kind == BONGO_CAT_BEHAVIOR_EFFECT)
                handled = bongo_cat_overlay_effect(app->overlay, NULL) || handled;
        } else if (!behavior->shortcut_active && bongo_cat_shortcut_matches(&app->shortcut_state,
            event, shortcut->shortcut) &&
            !hidden_toggle_has_visible_binding(app, behavior, shortcut->shortcut)) {
            behavior->shortcut_active = true;
            handled = bongo_cat_app_run_behavior(app, behavior) || handled;
        }
    }
    if (handled) return true;
    if (mver) return false;
    size_t limit = app->behaviors.count < 10 ? app->behaviors.count : 10;
    for (size_t i = 0; i < limit; ++i) {
        /* An explicit binding (including a cleared one) replaces the legacy alias. */
        if (bongo_cat_app_behavior_binding(app, app->behaviors.entries[i].id)) continue;
        if (app->behaviors.entries[i].kind == BONGO_CAT_BEHAVIOR_SOUND && !sound_edge) continue;
        char alias[8];
        snprintf(alias, sizeof(alias), "Alt+%c", i == 9 ? '0' : (char)('1' + i));
        if (bongo_cat_shortcut_matches(&app->shortcut_state, event, alias))
            return bongo_cat_app_run_behavior(app, &app->behaviors.entries[i]);
    }
    return false;
}

void bongo_cat_app_shortcuts(BongoCatApp *app, const BongoCatInputEvent *event) {
    if (!app || !event) return;
    bool sound_edge = false;
    bool sound_handled = bongo_cat_app_sound_shortcuts(app, event, &sound_edge);
    if (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON) {
        behavior_shortcut(app, event, sound_handled, sound_edge);
        return;
    }
    bool primary = bongo_cat_shortcut_update(&app->shortcut_state, event);
    /* Each pet handles its own behaviors; application-wide commands belong
       to the primary process (which also synchronizes child visibility). */
    if (!primary || app->secondary_pet) {
        behavior_shortcut(app, event, sound_handled, sound_edge);
        return;
    }
    BongoCatShortcutPreferences *shortcuts = &app->settings.shortcuts;
    if (bongo_cat_shortcut_matches(&app->shortcut_state, event,
        shortcuts->toggle_pet_visibility)) {
        toggle_pet_visibility(app);
    } else if (bongo_cat_shortcut_matches(&app->shortcut_state, event,
        shortcuts->visible_preferences)) {
        bongo_cat_preferences_visible(app->preferences) ?
            bongo_cat_preferences_close(app->preferences) : bongo_cat_preferences_show(app->preferences);
    } else if (bongo_cat_shortcut_matches(&app->shortcut_state, event,
        shortcuts->open_menu)) {
        /* Open after this input batch, so the modal loop cannot process a
           key release before the triggering key-down reaches the model. */
        if (app->context_menu_active) app->context_menu_close_requested = true;
        else app->context_menu_requested = !app->context_menu_requested;
    } else if (bongo_cat_shortcut_matches(&app->shortcut_state, event, shortcuts->mirror)) {
        app->settings.model.mirror = !app->settings.model.mirror;
        app->model_pointer_anchor_ready = false;
        app->pointer_known = false;
        app->dirty = true;
        bongo_cat_preferences_invalidate(app->preferences);
    } else if (bongo_cat_shortcut_matches(&app->shortcut_state, event, shortcuts->pass_through)) {
        app->settings.window.pass_through = !app->settings.window.pass_through;
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
        bongo_cat_preferences_invalidate(app->preferences);
    } else if (bongo_cat_shortcut_matches(&app->shortcut_state, event,
        shortcuts->always_on_top)) {
        app->settings.window.always_on_top = !app->settings.window.always_on_top;
        bongo_cat_platform_set_always_on_top(&app->platform, app->settings.window.always_on_top);
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
        bongo_cat_preferences_invalidate(app->preferences);
    } else behavior_shortcut(app, event, sound_handled, sound_edge);
}

static void test_key(BongoCatApp *app, BongoCatInputKind kind, const char *name) {
    BongoCatInputEvent event = {.kind = kind};
    snprintf(event.name, sizeof(event.name), "%s", name);
    bongo_cat_app_shortcuts(app, &event);
}

static void test_press(BongoCatApp *app, const char *name) {
    test_key(app, BONGO_CAT_INPUT_KEY_DOWN, name);
    test_key(app, BONGO_CAT_INPUT_KEY_UP, name);
}

bool bongo_cat_app_shortcuts_self_test(BongoCatApp *app) {
    if (!app || !app->preferences) return false;
    BongoCatShortcutPreferences *keys = &app->settings.shortcuts;
    snprintf(keys->toggle_pet_visibility,
        sizeof(keys->toggle_pet_visibility), "Control+B");
    snprintf(keys->visible_preferences, sizeof(keys->visible_preferences), "Control+Comma");
    snprintf(keys->mirror, sizeof(keys->mirror), "Control+M");
    snprintf(keys->pass_through, sizeof(keys->pass_through), "Control+P");
    snprintf(keys->always_on_top, sizeof(keys->always_on_top), "Control+T");
    app->session.window.visible = true;
    app->settings.model.mirror = false;
    app->model_pointer_anchor_ready = true;
    app->settings.window.pass_through = false;
    app->settings.window.always_on_top = false;
    test_key(app, BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft");
    test_press(app, "KeyB");
    test_press(app, "KeyM");
    test_press(app, "KeyP");
    test_press(app, "KeyT");
    test_press(app, "Comma");
    test_key(app, BONGO_CAT_INPUT_KEY_UP, "ControlLeft");
    bool result = !app->session.window.visible && app->settings.model.mirror &&
        !app->model_pointer_anchor_ready &&
        app->settings.window.pass_through && app->settings.window.always_on_top &&
        bongo_cat_preferences_visible(app->preferences);
    bongo_cat_preferences_close(app->preferences);
    bool ignored = app->smoke_ignore_global_input;
    app->smoke_ignore_global_input = false;
    test_key(app, BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft");
    BongoCatInputEvent release = {.kind = BONGO_CAT_INPUT_KEY_UP};
    snprintf(release.name, sizeof(release.name), "ControlLeft");
    bongo_cat_input_push(&app->input, &release);
    bongo_cat_app_drain_input(app, false);
    bool mirror = app->settings.model.mirror;
    test_press(app, "KeyM");
    result = result && !app->shortcut_state.control &&
        app->settings.model.mirror == mirror;
    app->smoke_ignore_global_input = ignored;
    return result;
}
