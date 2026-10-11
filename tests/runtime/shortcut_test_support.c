#include "shortcut_test_support.h"
#include "runtime.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/overlay.h"
#include "bongo_cat/preferences.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ShortcutTrace *shortcut_trace;
bool shortcut_record_actions = true;

static void record(char kind, int value, const char *argument) {
    if (!shortcut_record_actions) return;
    if (shortcut_trace->count == 4096) abort();
    ShortcutAction *action = &shortcut_trace->actions[shortcut_trace->count++];
    *action = (ShortcutAction){.kind = kind, .value = value};
    if (argument) snprintf(action->argument, sizeof(action->argument), "%s", argument);
}

void shortcut_binding(BongoCatApp *app, size_t slot, const char *id,
    const char *shortcut) {
    BongoCatBehaviorShortcut *binding = &app->config.behavior_shortcuts[slot];
    snprintf(binding->id, sizeof(binding->id), "%s", id);
    snprintf(binding->shortcut, sizeof(binding->shortcut), "%s", shortcut);
}

void shortcut_fixture(BongoCatApp *app, size_t behaviors, size_t bindings) {
    memset(app, 0, sizeof(*app));
    app->config.window.visible = true;
    app->model_pointer_anchor_ready = true;
    app->behaviors.count = behaviors;
    app->config.behavior_shortcut_count = bindings;
    for (size_t i = 0; i < behaviors; ++i) {
        BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        snprintf(entry->id, sizeof(entry->id), "model/motion/behavior-%03u", (unsigned)i);
        snprintf(entry->sound, sizeof(entry->sound), "sound-%03u.wav", (unsigned)i);
        snprintf(entry->effect, sizeof(entry->effect), "effect-%03u.png", (unsigned)i);
        snprintf(entry->group, sizeof(entry->group), "group-%03u", (unsigned)(i / 2));
        entry->index = (int)i;
        entry->kind = (BongoCatBehaviorKind)(i % 4);
        entry->momentary = i % 2 == 0;
    }
    for (size_t i = 0; i < bindings; ++i) {
        char id[BONGO_CAT_PATH_CAP], key[32];
        snprintf(id, sizeof(id), "model/motion/behavior-%03u",
            (unsigned)(behaviors ? i % behaviors : i));
        snprintf(key, sizeof(key), "Control+F%u", (unsigned)(i % 12 + 1));
        shortcut_binding(app, i, id, key);
    }
}

BongoCatInputEvent shortcut_event(BongoCatInputKind kind, const char *name,
    float value) {
    BongoCatInputEvent event = {.kind = kind, .value = value};
    snprintf(event.name, sizeof(event.name), "%s", name);
    return event;
}

bool bongo_cat_overlay_effect(BongoCatOverlay *overlay, const char *path) {
    (void)overlay;
    record('E', path != NULL, path);
    return !shortcut_trace->fail_effect;
}

BongoCatResult bongo_cat_audio_play(BongoCatAudio *audio, const char *path,
    BongoCatError *error) {
    (void)audio; (void)error;
    record('S', 0, path);
    return BONGO_CAT_OK;
}

void bongo_cat_audio_stop(BongoCatAudio *audio) {
    (void)audio;
    record('X', 0, NULL);
}

bool bongo_cat_live2d_start_motion(BongoCatLive2D *live2d,
    const char *group, int index) {
    (void)live2d;
    record('M', index, group);
    return !shortcut_trace->fail_motion;
}

bool bongo_cat_live2d_set_expression(BongoCatLive2D *live2d, int index) {
    (void)live2d;
    record('Q', index, NULL);
    if (shortcut_trace->fail_expression) return false;
    shortcut_trace->expression = index;
    return true;
}

void bongo_cat_window_set_visible(BongoCatApp *app, bool visible) {
    record('V', visible, NULL);
    app->config.window.visible = visible;
}

bool bongo_cat_preferences_visible(const BongoCatPreferences *preferences) {
    (void)preferences;
    return shortcut_trace->preferences_visible;
}

void bongo_cat_preferences_close(BongoCatPreferences *preferences) {
    (void)preferences;
    record('P', 0, NULL);
    shortcut_trace->preferences_visible = false;
}

void bongo_cat_preferences_show(BongoCatPreferences *preferences) {
    (void)preferences;
    record('P', 1, NULL);
    shortcut_trace->preferences_visible = true;
}

void bongo_cat_preferences_invalidate(BongoCatPreferences *preferences) {
    (void)preferences;
    record('I', 0, NULL);
}

void bongo_cat_window_mark_hit_dirty(BongoCatApp *app) {
    app->pointer_hit_dirty = true;
    record('H', 0, NULL);
}

void bongo_cat_window_sync_click_through(BongoCatApp *app) {
    (void)app;
    record('T', 0, NULL);
}

void bongo_cat_platform_set_always_on_top(BongoCatPlatform *platform, bool enabled) {
    (void)platform;
    record('A', enabled, NULL);
}
