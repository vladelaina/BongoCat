#include "runtime.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/preferences.h"
#include "bongo_cat/shortcut.h"

#include <stdio.h>
#include <string.h>

static void visible(BongoCatApp *app) {
    bongo_cat_window_set_visible(app, !app->config.window.visible);
}

bool bongo_cat_app_run_behavior(BongoCatApp *app,
    const BongoCatBehaviorEntry *behavior) {
    if (!app || !behavior) return false;
    if (behavior->kind == BONGO_CAT_BEHAVIOR_EFFECT) {
        if (!bongo_cat_overlay_effect(app->overlay, behavior->effect)) return false;
    } else if (behavior->kind == BONGO_CAT_BEHAVIOR_SOUND) {
        if (!behavior->sound[0]) {
            bongo_cat_audio_stop(app->audio);
            return true;
        }
        BongoCatError error = {0};
        bongo_cat_audio_play(app->audio, behavior->sound, &error);
    } else if (behavior->kind == BONGO_CAT_BEHAVIOR_MOTION) {
        bool started = bongo_cat_live2d_start_motion(app->live2d,
            behavior->group, behavior->index);
        if (!started) return false;
        if (behavior->sound[0]) {
            BongoCatError error = {0};
            bongo_cat_audio_play(app->audio, behavior->sound, &error);
        }
    } else if (!bongo_cat_live2d_set_expression(app->live2d, behavior->index)) return false;
    app->dirty = true;
    return true;
}

static bool behavior_shortcut(BongoCatApp *app, const BongoCatInputEvent *event) {
    bool handled = false;
    if (!app->behaviors.count) return false;
    /* Parsing stale bindings can cost more than scanning a tiny catalog. */
    bool prefilter = app->behaviors.count >= 32;
    for (size_t i = 0; i < app->config.behavior_shortcut_count; ++i) {
        BongoCatBehaviorShortcut *shortcut = &app->config.behavior_shortcuts[i];
        /* Keep the ID-first action path unchanged, including matcher rechecks. */
        if (prefilter &&
            !bongo_cat_shortcut_release_matches(event, shortcut->shortcut) &&
            !bongo_cat_shortcut_matches(&app->shortcut_state,
                event, shortcut->shortcut)) continue;
        for (size_t j = 0; j < app->behaviors.count; ++j) {
            BongoCatBehaviorEntry *behavior = &app->behaviors.entries[j];
            if (strcmp(shortcut->id, behavior->id) != 0) continue;
            if (behavior->momentary &&
                bongo_cat_shortcut_release_matches(event, shortcut->shortcut)) {
                if (behavior->kind == BONGO_CAT_BEHAVIOR_EFFECT)
                    handled = bongo_cat_overlay_effect(app->overlay, NULL) || handled;
                else if (behavior->kind == BONGO_CAT_BEHAVIOR_SOUND) {
                    bongo_cat_audio_stop(app->audio);
                    handled = true;
                }
            } else if (bongo_cat_shortcut_matches(&app->shortcut_state,
                event, shortcut->shortcut)) handled =
                    bongo_cat_app_run_behavior(app, behavior) || handled;
        }
    }
    if (handled) return true;
    size_t limit = app->behaviors.count < 10 ? app->behaviors.count : 10;
    for (size_t i = 0; i < limit; ++i) {
        char alias[8];
        snprintf(alias, sizeof(alias), "Alt+%c", i == 9 ? '0' : (char)('1' + i));
        if (bongo_cat_shortcut_matches(&app->shortcut_state, event, alias))
            return bongo_cat_app_run_behavior(app, &app->behaviors.entries[i]);
    }
    return false;
}

void bongo_cat_app_shortcuts(BongoCatApp *app, const BongoCatInputEvent *event) {
    if (!app) return;
    if (event->kind == BONGO_CAT_INPUT_GAMEPAD_BUTTON) {
        behavior_shortcut(app, event);
        return;
    }
    bool primary = bongo_cat_shortcut_update(&app->shortcut_state, event);
    if (!primary) {
        behavior_shortcut(app, event);
        return;
    }
    BongoCatShortcutOptions *shortcuts = &app->config.shortcuts;
    if (bongo_cat_shortcut_matches(&app->shortcut_state, event, shortcuts->visible_cat)) {
        visible(app);
    } else if (bongo_cat_shortcut_matches(&app->shortcut_state, event,
        shortcuts->visible_preferences)) {
        bongo_cat_preferences_visible(app->preferences) ?
            bongo_cat_preferences_close(app->preferences) : bongo_cat_preferences_show(app->preferences);
    } else if (bongo_cat_shortcut_matches(&app->shortcut_state, event, shortcuts->mirror)) {
        app->config.model.mirror = !app->config.model.mirror;
        app->model_pointer_anchor_ready = false;
        app->dirty = true;
        bongo_cat_preferences_invalidate(app->preferences);
    } else if (bongo_cat_shortcut_matches(&app->shortcut_state, event, shortcuts->pass_through)) {
        app->config.window.pass_through = !app->config.window.pass_through;
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
        bongo_cat_preferences_invalidate(app->preferences);
    } else if (bongo_cat_shortcut_matches(&app->shortcut_state, event,
        shortcuts->always_on_top)) {
        app->config.window.always_on_top = !app->config.window.always_on_top;
        bongo_cat_platform_set_always_on_top(&app->platform, app->config.window.always_on_top);
        bongo_cat_window_mark_hit_dirty(app);
        bongo_cat_window_sync_click_through(app);
        bongo_cat_preferences_invalidate(app->preferences);
    } else behavior_shortcut(app, event);
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
    BongoCatShortcutOptions *keys = &app->config.shortcuts;
    snprintf(keys->visible_cat, sizeof(keys->visible_cat), "Control+B");
    snprintf(keys->visible_preferences, sizeof(keys->visible_preferences), "Control+Comma");
    snprintf(keys->mirror, sizeof(keys->mirror), "Control+M");
    snprintf(keys->pass_through, sizeof(keys->pass_through), "Control+P");
    snprintf(keys->always_on_top, sizeof(keys->always_on_top), "Control+T");
    app->config.window.visible = true;
    app->config.model.mirror = false;
    app->model_pointer_anchor_ready = true;
    app->config.window.pass_through = false;
    app->config.window.always_on_top = false;
    test_key(app, BONGO_CAT_INPUT_KEY_DOWN, "ControlLeft");
    test_press(app, "KeyB");
    test_press(app, "KeyM");
    test_press(app, "KeyP");
    test_press(app, "KeyT");
    test_press(app, "Comma");
    test_key(app, BONGO_CAT_INPUT_KEY_UP, "ControlLeft");
    bool result = !app->config.window.visible && app->config.model.mirror &&
        !app->model_pointer_anchor_ready &&
        app->config.window.pass_through && app->config.window.always_on_top &&
        bongo_cat_preferences_visible(app->preferences);
    bongo_cat_preferences_close(app->preferences);
    return result;
}
