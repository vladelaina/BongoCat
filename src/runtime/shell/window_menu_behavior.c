#include "window_menu.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/shortcut.h"

#include <stdio.h>
#include <string.h>

static const BongoCatBehaviorEntry *nth_behavior(BongoCatApp *app,
    BongoCatBehaviorKind kind, size_t position) {
    if (!app) return NULL;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != kind) continue;
        if (kind == BONGO_CAT_BEHAVIOR_SOUND && !entry->sound[0] && !entry->sound_clear) continue;
        if (kind == BONGO_CAT_BEHAVIOR_MOTION &&
            !bongo_cat_live2d_motion_visible(app->live2d,
                entry->group, entry->index)) continue;
        if (!position) return entry;
        position--;
    }
    return NULL;
}

static const char *behavior_shortcut(const BongoCatApp *app, const char *id) {
    if (!app || !id) return NULL;
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *binding = &app->settings.behavior_shortcuts[i];
        if (strcmp(binding->id, id) == 0) return binding->shortcut;
    }
    return NULL;
}

static bool hidden_toggle_has_visible_binding(BongoCatApp *app,
    const BongoCatBehaviorEntry *entry, const char *shortcut) {
    if (!app || !entry || !shortcut || !shortcut[0] ||
        entry->kind != BONGO_CAT_BEHAVIOR_MOTION ||
        bongo_cat_live2d_motion_visible(app->live2d,
            entry->group, entry->index)) return false;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *candidate = &app->behaviors.entries[i];
        const char *bound = behavior_shortcut(app, candidate->id);
        if (candidate != entry &&
            candidate->kind == BONGO_CAT_BEHAVIOR_MOTION &&
            bongo_cat_live2d_motion_visible(app->live2d,
                candidate->group, candidate->index) &&
            bongo_cat_live2d_motion_same_toggle(app->live2d,
                entry->group, entry->index,
                candidate->group, candidate->index) &&
            bound && strcmp(bound, shortcut) == 0) return true;
    }
    return false;
}

static const char *behavior_label(const BongoCatApp *app,
    const BongoCatBehaviorEntry *entry) {
    if (!app || !entry) return "";
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *binding = &app->settings.behavior_shortcuts[i];
        if (strcmp(binding->id, entry->id) == 0 && binding->label[0])
            return binding->label;
    }
    if (entry->sound_clear) return bongo_cat_i18n_get(app->i18n,
        "pages.preference.model.behaviorModal.labels.stopAllAudio", "Stop all audio");
    return entry->label;
}

static void menu_label(char output[BONGO_CAT_MENU_LABEL_CAP],
    const BongoCatApp *app, const BongoCatBehaviorEntry *entry) {
    const char *label = behavior_label(app, entry);
    const char *shortcut = behavior_shortcut(app, entry->id);
    char shortcut_label[BONGO_CAT_SHORTCUT_CAP * 2];
    bongo_cat_shortcut_format(shortcut, shortcut_label, sizeof(shortcut_label));
    if (shortcut && shortcut[0]) snprintf(output, BONGO_CAT_MENU_LABEL_CAP,
        "%s - %s", label, shortcut_label);
    else snprintf(output, BONGO_CAT_MENU_LABEL_CAP, "%s", label);
}

static bool run_binding(BongoCatApp *app, const BongoCatBehaviorEntry *selected) {
    const char *shortcut = behavior_shortcut(app, selected->id);
    bool handled = bongo_cat_app_run_behavior(app, selected);
    if (!shortcut || !shortcut[0]) return handled;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        const char *bound = behavior_shortcut(app, entry->id);
        if (entry != selected && bound && strcmp(bound, shortcut) == 0 &&
            !hidden_toggle_has_visible_binding(app, entry, shortcut))
            handled = bongo_cat_app_run_behavior(app, entry) || handled;
    }
    return handled;
}

void bongo_cat_window_behavior_labels(BongoCatApp *app,
    char motions[][BONGO_CAT_MENU_LABEL_CAP], bool *motion_checked,
    size_t *motion_count, char expressions[][BONGO_CAT_MENU_LABEL_CAP],
    size_t *expression_count,
    size_t *current_expression) {
    if (!motion_count || !expression_count) return;
    *motion_count = 0; *expression_count = 0;
    if (current_expression) *current_expression = BONGO_CAT_BEHAVIOR_CAP;
    if (!app) return;
    int active_expression = bongo_cat_live2d_expression(app->live2d);
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind == BONGO_CAT_BEHAVIOR_MOTION && motions) {
            if (!bongo_cat_live2d_motion_visible(app->live2d,
                entry->group, entry->index)) continue;
            if (motion_checked) motion_checked[*motion_count] =
                bongo_cat_live2d_motion_selected(app->live2d,
                    entry->group, entry->index);
            menu_label(motions[*motion_count], app, entry);
            (*motion_count)++;
        } else if (entry->kind == BONGO_CAT_BEHAVIOR_EXPRESSION && expressions) {
            if (current_expression && entry->index == active_expression)
                *current_expression = *expression_count;
            menu_label(expressions[*expression_count], app, entry);
            (*expression_count)++;
        }
    }
}

bool bongo_cat_window_behavior_action(BongoCatApp *app,
    BongoCatMenuAction action) {
    BongoCatBehaviorKind kind;
    size_t position;
    if (action >= BONGO_CAT_MENU_MOTION_FIRST &&
        action < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP) {
        kind = BONGO_CAT_BEHAVIOR_MOTION;
        position = (size_t)(action - BONGO_CAT_MENU_MOTION_FIRST);
    } else if (action >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
        action < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP) {
        kind = BONGO_CAT_BEHAVIOR_EXPRESSION;
        position = (size_t)(action - BONGO_CAT_MENU_EXPRESSION_FIRST);
    } else if (action >= BONGO_CAT_MENU_AUDIO_FIRST &&
        action < BONGO_CAT_MENU_AUDIO_FIRST + BONGO_CAT_BEHAVIOR_CAP) {
        const BongoCatBehaviorEntry *sound = nth_behavior(app, BONGO_CAT_BEHAVIOR_SOUND,
            (size_t)(action - BONGO_CAT_MENU_AUDIO_FIRST));
        if (!sound) return false;
        if (bongo_cat_audio_is_playing(app->audio, sound->sound))
            bongo_cat_audio_stop_path(app->audio, sound->sound);
        else return bongo_cat_app_run_behavior(app, sound);
        return true;
    } else return false;
    const BongoCatBehaviorEntry *entry = nth_behavior(app, kind, position);
    return entry && run_binding(app, entry);
}

bool bongo_cat_window_behavior_preview(BongoCatApp *app,
    BongoCatMenuAction action) {
    if (!app) return false;
    if (action >= BONGO_CAT_MENU_MOTION_FIRST &&
        action < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP) {
        size_t position = (size_t)(action - BONGO_CAT_MENU_MOTION_FIRST);
        const BongoCatBehaviorEntry *entry = nth_behavior(app,
            BONGO_CAT_BEHAVIOR_MOTION, position);
        return entry && bongo_cat_live2d_preview_motion(app->live2d,
            entry->group, entry->index);
    }
    if (action >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
        action < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP) {
        size_t position = (size_t)(action - BONGO_CAT_MENU_EXPRESSION_FIRST);
        const BongoCatBehaviorEntry *entry = nth_behavior(app,
            BONGO_CAT_BEHAVIOR_EXPRESSION, position);
        if (!entry) return false;
        int target = bongo_cat_live2d_expression(app->live2d) == entry->index ?
            -1 : entry->index;
        return bongo_cat_live2d_set_expression(app->live2d, target);
    }
    return false;
}

bool bongo_cat_window_behavior_commit_preview(BongoCatApp *app,
    BongoCatMenuAction action) {
    if (!app || action < BONGO_CAT_MENU_MOTION_FIRST ||
        action >= BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP)
        return false;
    const BongoCatBehaviorEntry *entry = nth_behavior(app,
        BONGO_CAT_BEHAVIOR_MOTION,
        (size_t)(action - BONGO_CAT_MENU_MOTION_FIRST));
    if (!entry || !bongo_cat_live2d_commit_motion_preview(app->live2d,
        entry->group, entry->index)) return false;
    if (entry->sound[0]) {
        BongoCatError error = {0};
        bongo_cat_audio_play(app->audio, entry->sound, &error);
    }
    const char *shortcut = behavior_shortcut(app, entry->id);
    if (shortcut && shortcut[0]) for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *companion = &app->behaviors.entries[i];
        const char *bound = behavior_shortcut(app, companion->id);
        if (companion != entry && bound && strcmp(bound, shortcut) == 0 &&
            !hidden_toggle_has_visible_binding(app, companion, shortcut))
            bongo_cat_app_run_behavior(app, companion);
    }
    bongo_cat_app_capture_behavior_state(app);
    app->dirty = true;
    return true;
}

bool bongo_cat_window_behavior_menu_action(BongoCatMenuAction action) {
    return (action >= BONGO_CAT_MENU_MOTION_FIRST &&
        action < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP) ||
        (action >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
            action < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP);
}

void bongo_cat_window_audio_labels(BongoCatApp *app,
    char names[][BONGO_CAT_MENU_LABEL_CAP], bool *checked, size_t *count) {
    *count = 0;
    bool available = false;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != BONGO_CAT_BEHAVIOR_SOUND || (!entry->sound[0] && !entry->sound_clear)) continue;
        available = available || entry->sound[0];
        menu_label(names[*count], app, entry);
        checked[*count] = bongo_cat_audio_is_playing(app->audio, entry->sound);
        ++*count;
    }
    if (!available) *count = 0;
}
