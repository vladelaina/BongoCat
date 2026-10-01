#include "window_menu.h"
#include "bongo_cat/shortcut.h"

#include <stdio.h>
#include <string.h>

static const BongoCatBehaviorEntry *test_nth_behavior(BongoCatApp *app,
    BongoCatBehaviorKind kind, size_t position) {
    if (!app) return NULL;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != kind) continue;
        if (kind == BONGO_CAT_BEHAVIOR_MOTION &&
            !bongo_cat_live2d_motion_visible(app->live2d,
                entry->group, entry->index)) continue;
        if (!position) return entry;
        position--;
    }
    return NULL;
}

static const char *test_behavior_shortcut(const BongoCatApp *app,
    const char *id) {
    if (!app || !id) return NULL;
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        const BongoCatBehaviorShortcut *binding =
            &app->settings.behavior_shortcuts[i];
        if (strcmp(binding->id, id) == 0) return binding->shortcut;
    }
    return NULL;
}

static bool expression_self_test(BongoCatApp *app) {
    size_t count = 0;
    while (test_nth_behavior(app, BONGO_CAT_BEHAVIOR_EXPRESSION, count)) count++;
    if (!count) return true;
    int original = bongo_cat_live2d_expression(app->live2d);
    char motions[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
    bool motion_checked[BONGO_CAT_BEHAVIOR_CAP] = {false};
    char expressions[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
    size_t motion_count, expression_count, current_expression;
    bool passed = bongo_cat_live2d_set_expression(app->live2d, -1);
    bongo_cat_window_behavior_labels(app, motions, motion_checked, &motion_count,
        expressions, &expression_count, &current_expression);
    passed = passed && expression_count == count &&
        current_expression == BONGO_CAT_BEHAVIOR_CAP;
    size_t selected_position = count > 2 ? 2 : count - 1;
    const BongoCatBehaviorEntry *selected = test_nth_behavior(app,
        BONGO_CAT_BEHAVIOR_EXPRESSION, selected_position);
    passed = selected && bongo_cat_window_behavior_action(app,
        BONGO_CAT_MENU_EXPRESSION_FIRST + selected_position) &&
        bongo_cat_live2d_expression(app->live2d) == selected->index && passed;
    bongo_cat_window_behavior_labels(app, motions, motion_checked, &motion_count,
        expressions, &expression_count, &current_expression);
    passed = passed && expression_count == count &&
        current_expression == selected_position;
    if (passed && count > 1) {
        size_t alternate_position = selected_position ? 0 : 1;
        const BongoCatBehaviorEntry *alternate = test_nth_behavior(app,
            BONGO_CAT_BEHAVIOR_EXPRESSION, alternate_position);
        BongoCatWindowMenuPreview preview;
        bongo_cat_window_menu_preview_init(&preview, app);
        bongo_cat_window_menu_preview(&preview,
            BONGO_CAT_MENU_EXPRESSION_FIRST + alternate_position);
        passed = alternate && bongo_cat_live2d_expression(app->live2d) ==
            alternate->index;
        bongo_cat_window_menu_restore(&preview, BONGO_CAT_MENU_NONE);
        passed = passed && bongo_cat_live2d_expression(app->live2d) ==
            selected->index;
        bongo_cat_window_menu_preview_init(&preview, app);
        bongo_cat_window_menu_preview(&preview,
            BONGO_CAT_MENU_EXPRESSION_FIRST + alternate_position);
        bongo_cat_window_menu_restore(&preview,
            BONGO_CAT_MENU_EXPRESSION_FIRST + alternate_position);
        passed = passed && bongo_cat_live2d_expression(app->live2d) ==
            alternate->index && bongo_cat_window_menu_preview_applied(&preview,
                BONGO_CAT_MENU_EXPRESSION_FIRST + alternate_position);
    }
    if (passed) {
        bongo_cat_live2d_set_expression(app->live2d, selected->index);
        BongoCatWindowMenuPreview preview;
        bongo_cat_window_menu_preview_init(&preview, app);
        BongoCatMenuAction selected_action =
            BONGO_CAT_MENU_EXPRESSION_FIRST + selected_position;
        bongo_cat_window_menu_preview(&preview, selected_action);
        passed = bongo_cat_live2d_expression(app->live2d) == -1;
        bongo_cat_window_menu_restore(&preview, selected_action);
        passed = passed && bongo_cat_live2d_expression(app->live2d) == -1 &&
            bongo_cat_window_menu_preview_applied(&preview, selected_action);
    }
    bool restored = bongo_cat_live2d_set_expression(app->live2d, original);
    if (!passed || !restored) SDL_Log("Expression menu self-test failed: "
        "count=%llu mapped=%llu current=%llu original=%d restored=%d",
        (unsigned long long)count, (unsigned long long)expression_count,
        (unsigned long long)current_expression, original, restored);
    return restored && passed;
}

static void advance_motion(BongoCatApp *app, unsigned steps) {
    for (unsigned i = 0; i < steps; ++i)
        bongo_cat_app_step_live2d(app, 0.25f);
}

static bool motion_lifecycle_self_test(BongoCatApp *app) {
    const BongoCatBehaviorEntry *persistent = NULL, *one_shot = NULL;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != BONGO_CAT_BEHAVIOR_MOTION ||
            !bongo_cat_live2d_motion_visible(app->live2d,
                entry->group, entry->index)) continue;
        if (bongo_cat_live2d_motion_persistent(app->live2d,
            entry->group, entry->index)) {
            if (!persistent) persistent = entry;
        } else if (!one_shot) one_shot = entry;
    }
    bool persistent_kept = true, checked = true, cleared = true;
    bool isolated = true, started = true;
    if (persistent) {
        if (bongo_cat_live2d_motion_selected(app->live2d,
            persistent->group, persistent->index)) {
            started = bongo_cat_live2d_start_motion(app->live2d,
                persistent->group, persistent->index) && started;
            advance_motion(app, 8);
        }
        started = bongo_cat_live2d_start_motion(app->live2d,
            persistent->group, persistent->index) && started;
        advance_motion(app, 240);
        persistent_kept = bongo_cat_live2d_motion_selected(app->live2d,
            persistent->group, persistent->index);
    }
    unsigned steps = 0;
    if (one_shot) {
        started = bongo_cat_live2d_start_motion(app->live2d,
            one_shot->group, one_shot->index) && started;
        checked = bongo_cat_live2d_motion_selected(app->live2d,
            one_shot->group, one_shot->index);
        while (steps++ < 1200 && bongo_cat_live2d_motion_selected(app->live2d,
            one_shot->group, one_shot->index)) advance_motion(app, 1);
        cleared = !bongo_cat_live2d_motion_selected(app->live2d,
            one_shot->group, one_shot->index);
        isolated = !persistent || bongo_cat_live2d_motion_selected(app->live2d,
            persistent->group, persistent->index);
    }
    if (persistent && bongo_cat_live2d_motion_selected(app->live2d,
        persistent->group, persistent->index))
        bongo_cat_live2d_start_motion(app->live2d,
            persistent->group, persistent->index);
    bool passed = started && persistent_kept && checked && cleared && isolated;
    SDL_Log("Motion lifecycle self-test: persistent=%d one_shot=%d "
        "checked=%d cleared=%d isolated=%d steps=%u result=%d",
        persistent != NULL, one_shot != NULL, checked, cleared, isolated,
        steps, passed);
    return passed;
}

bool bongo_cat_window_behavior_self_test(BongoCatApp *app) {
    if (!app) return false;
    bool passed = true;
    if (test_nth_behavior(app, BONGO_CAT_BEHAVIOR_MOTION, 0)) {
        const BongoCatBehaviorEntry *first = test_nth_behavior(app,
            BONGO_CAT_BEHAVIOR_MOTION, 0);
        passed = bongo_cat_window_behavior_action(app,
            BONGO_CAT_MENU_MOTION_FIRST) && passed;
        char motions[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
        bool checked[BONGO_CAT_BEHAVIOR_CAP] = {false};
        bool before[BONGO_CAT_BEHAVIOR_CAP] = {false};
        char expressions[BONGO_CAT_BEHAVIOR_CAP][BONGO_CAT_MENU_LABEL_CAP];
        size_t motion_count, expression_count, current_expression;
        bongo_cat_window_behavior_labels(app, motions, checked, &motion_count,
            expressions, &expression_count, &current_expression);
        passed = motion_count > 0 && checked[0] && passed;
        bool initial_passed = passed;
        const char *first_shortcut = test_behavior_shortcut(app, first->id);
        if (first_shortcut && first_shortcut[0]) {
            char shortcut_label[BONGO_CAT_SHORTCUT_CAP * 2];
            bongo_cat_shortcut_format(first_shortcut, shortcut_label, sizeof(shortcut_label));
            passed = strstr(motions[0], shortcut_label) != NULL && passed;
        }
        memcpy(before, checked, motion_count * sizeof(before[0]));
        size_t preview_position = test_nth_behavior(app,
            BONGO_CAT_BEHAVIOR_MOTION, 1) ? 1 : 0;
        BongoCatWindowMenuPreview preview;
        bongo_cat_window_menu_preview_init(&preview, app);
        bongo_cat_window_menu_preview(&preview,
            BONGO_CAT_MENU_MOTION_FIRST + preview_position);
        bongo_cat_window_behavior_labels(app, motions, checked, &motion_count,
            expressions, &expression_count, &current_expression);
        bool preview_stable = memcmp(before, checked,
            motion_count * sizeof(before[0])) == 0;
        passed = preview_stable && passed;
        bongo_cat_window_menu_restore(&preview, BONGO_CAT_MENU_NONE);
        passed = bongo_cat_live2d_motion_selected(app->live2d,
            first->group, first->index) && passed;
        passed = bongo_cat_window_behavior_action(app,
            BONGO_CAT_MENU_MOTION_FIRST + preview_position) && passed;
        bongo_cat_window_behavior_labels(app, motions, checked, &motion_count,
            expressions, &expression_count, &current_expression);
        passed = checked[preview_position] && passed;
        const BongoCatBehaviorEntry *preview_entry = test_nth_behavior(app,
            BONGO_CAT_BEHAVIOR_MOTION, preview_position);
        BongoCatWindowMenuPreview cancel_preview;
        bongo_cat_window_menu_preview_init(&cancel_preview, app);
        bongo_cat_window_menu_preview(&cancel_preview,
            BONGO_CAT_MENU_MOTION_FIRST + preview_position);
        bongo_cat_window_menu_restore(&cancel_preview,
            BONGO_CAT_MENU_MOTION_FIRST + preview_position);
        passed = preview_entry && bongo_cat_window_menu_preview_applied(
            &cancel_preview, BONGO_CAT_MENU_MOTION_FIRST + preview_position) &&
            !bongo_cat_live2d_motion_selected(app->live2d,
                preview_entry->group, preview_entry->index) && passed;
        bool independent = false;
        for (size_t i = 1; i < motion_count && !independent; ++i) {
            passed = bongo_cat_window_behavior_action(app,
                BONGO_CAT_MENU_MOTION_FIRST) && passed;
            passed = bongo_cat_window_behavior_action(app,
                BONGO_CAT_MENU_MOTION_FIRST + i) && passed;
            bool first_selected = bongo_cat_live2d_motion_selected(app->live2d,
                first->group, first->index);
            const BongoCatBehaviorEntry *candidate = test_nth_behavior(app,
                BONGO_CAT_BEHAVIOR_MOTION, i);
            bool candidate_selected = bongo_cat_live2d_motion_selected(app->live2d,
                candidate->group, candidate->index);
            independent = independent || (first_selected && candidate_selected);
        }
        if (motion_count > 2 && strcmp(first->group, "CAT_motion_lock") == 0)
            passed = independent && passed;
        passed = bongo_cat_window_behavior_action(app,
            BONGO_CAT_MENU_MOTION_FIRST) && passed;
        bool paired = false;
        for (size_t i = 0; i < app->behaviors.count; ++i) {
            const BongoCatBehaviorEntry *item = &app->behaviors.entries[i];
            if (item->kind == BONGO_CAT_BEHAVIOR_MOTION &&
                !bongo_cat_live2d_motion_visible(app->live2d,
                    item->group, item->index) &&
                bongo_cat_live2d_motion_same_toggle(app->live2d,
                    first->group, first->index,
                    item->group, item->index)) paired = true;
        }
        bool toggled_off = !bongo_cat_live2d_motion_selected(app->live2d,
            first->group, first->index);
        passed = (!paired || toggled_off) && passed;
        if (!passed) SDL_Log("Motion menu self-test failed: initial=%d "
            "preview=%d count=%llu independent=%d off=%d",
            initial_passed, preview_stable, (unsigned long long)motion_count,
            independent, toggled_off);
    }
    passed = motion_lifecycle_self_test(app) && passed;
    passed = expression_self_test(app) && passed;
    return passed;
}
