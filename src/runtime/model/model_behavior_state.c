#include "bongo_cat/app.h"

#include <stdio.h>
#include <string.h>

static bool behavior_active(const BongoCatApp *app,
    const BongoCatBehaviorEntry *entry, int expression) {
    if (entry->kind == BONGO_CAT_BEHAVIOR_MOTION) {
        if (!bongo_cat_live2d_motion_visible(app->live2d,
            entry->group, entry->index)) return false;
        return bongo_cat_live2d_motion_persistent(app->live2d,
            entry->group, entry->index) &&
            bongo_cat_live2d_motion_selected(app->live2d,
                entry->group, entry->index);
    }
    return entry->kind == BONGO_CAT_BEHAVIOR_EXPRESSION &&
        entry->index == expression;
}

size_t bongo_cat_app_selected_motion_count(const BongoCatApp *app) {
    if (!app || !app->live2d) return 0;
    size_t count = 0;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind == BONGO_CAT_BEHAVIOR_MOTION &&
            bongo_cat_live2d_motion_visible(app->live2d,
                entry->group, entry->index) &&
            bongo_cat_live2d_motion_selected(app->live2d,
                entry->group, entry->index)) count++;
    }
    return count;
}

static void remove_model_state(BongoCatSessionState *session,
    const char *model_id) {
    size_t count = session->active_behavior_count;
    if (count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    size_t output = 0;
    for (size_t i = 0; i < count; ++i) {
        BongoCatActiveBehavior entry = session->active_behaviors[i];
        if (strcmp(entry.model_id, model_id) != 0)
            session->active_behaviors[output++] = entry;
    }
    session->active_behavior_count = output;
}

void bongo_cat_app_forget_behavior_state(BongoCatApp *app,
    const char *model_id) {
    if (!app || !model_id || !model_id[0]) return;
    remove_model_state(&app->session, model_id);
}

static void make_room(BongoCatSessionState *session, size_t required) {
    size_t count = session->active_behavior_count;
    if (required > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        required = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    if (count + required <= BONGO_CAT_BEHAVIOR_BINDING_CAP) return;
    size_t remove = count + required - BONGO_CAT_BEHAVIOR_BINDING_CAP;
    memmove(session->active_behaviors, &session->active_behaviors[remove],
        (count - remove) * sizeof(session->active_behaviors[0]));
    session->active_behavior_count -= remove;
}

void bongo_cat_app_capture_behavior_state(BongoCatApp *app) {
    if (!app || !app->live2d || !app->loaded_model[0]) return;
    int expression = bongo_cat_live2d_expression(app->live2d);
    size_t active = 0;
    for (size_t i = 0; i < app->behaviors.count; ++i)
        if (behavior_active(app, &app->behaviors.entries[i], expression))
            active++;
    remove_model_state(&app->session, app->loaded_model);
    make_room(&app->session, active);
    for (size_t i = 0; i < app->behaviors.count &&
        app->session.active_behavior_count <
            BONGO_CAT_BEHAVIOR_BINDING_CAP; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (!behavior_active(app, entry, expression)) continue;
        BongoCatActiveBehavior *saved = &app->session.active_behaviors[
            app->session.active_behavior_count++];
        memset(saved, 0, sizeof(*saved));
        snprintf(saved->model_id, sizeof(saved->model_id), "%s",
            app->loaded_model);
        snprintf(saved->behavior_id, sizeof(saved->behavior_id), "%.*s",
            (int)sizeof(saved->behavior_id) - 1, entry->id);
    }
}

size_t bongo_cat_app_saved_behavior_count(const BongoCatApp *app,
    const char *model_id) {
    if (!app || !model_id) return 0;
    size_t limit = app->session.active_behavior_count;
    if (limit > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        limit = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    size_t count = 0;
    for (size_t i = 0; i < limit; ++i)
        if (strcmp(app->session.active_behaviors[i].model_id, model_id) == 0)
            count++;
    return count;
}

static const BongoCatBehaviorEntry *find_behavior(const BongoCatApp *app,
    const char *id) {
    for (size_t i = 0; i < app->behaviors.count; ++i)
        if (strcmp(app->behaviors.entries[i].id, id) == 0)
            return &app->behaviors.entries[i];
    return NULL;
}

void bongo_cat_app_restore_behavior_state(BongoCatApp *app,
    const char *model_id) {
    if (!app || !app->live2d || !model_id || !model_id[0]) return;
    size_t count = app->session.active_behavior_count;
    if (count > BONGO_CAT_BEHAVIOR_BINDING_CAP)
        count = BONGO_CAT_BEHAVIOR_BINDING_CAP;
    for (size_t i = 0; i < count; ++i) {
        const BongoCatActiveBehavior *saved =
            &app->session.active_behaviors[i];
        if (strcmp(saved->model_id, model_id) != 0) continue;
        const BongoCatBehaviorEntry *entry = find_behavior(app,
            saved->behavior_id);
        if (!entry) continue;
        if (entry->kind == BONGO_CAT_BEHAVIOR_MOTION &&
            bongo_cat_live2d_motion_visible(app->live2d,
                entry->group, entry->index) &&
            bongo_cat_live2d_motion_persistent(app->live2d,
                entry->group, entry->index) &&
            !bongo_cat_live2d_motion_selected(app->live2d,
                entry->group, entry->index))
            bongo_cat_live2d_restore_motion_state(app->live2d,
                entry->group, entry->index);
        else if (entry->kind == BONGO_CAT_BEHAVIOR_EXPRESSION)
            bongo_cat_live2d_set_expression(app->live2d, entry->index);
    }
}
