#include "runtime.h"

#define NANOSECONDS_PER_SECOND 1000000000ull

static uint32_t random_behavior_next(BongoCatApp *app, uint64_t now) {
    uint32_t state = app->random_behavior_state;
    if (!state) {
        state = (uint32_t)now ^ (uint32_t)(now >> 32) ^ 0x9e3779b9u;
        const unsigned char *text = (const unsigned char *)app->loaded_model;
        while (*text) state = (state ^ *text++) * 16777619u;
        if (!state) state = 0x6d2b79f5u;
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    app->random_behavior_state = state;
    return state;
}

void bongo_cat_random_behavior_reset(BongoCatApp *app) {
    if (!app) return;
    app->random_expression_due_ns = 0;
    app->random_expression_interval_seconds = 0.0f;
    app->random_motion_due_ns = 0;
    app->random_motion_interval_seconds = 0.0f;
}

static bool random_behavior_due(bool enabled, float seconds, float fallback,
    uint64_t now, uint64_t *due_ns, float *interval_seconds) {
    if (!enabled) {
        *due_ns = 0;
        *interval_seconds = 0.0f;
        return false;
    }
    if (!(seconds >= 1.0f && seconds <= 3600.0f)) seconds = fallback;
    uint64_t interval = (uint64_t)(seconds * (float)NANOSECONDS_PER_SECOND);
    if (!*due_ns || *interval_seconds != seconds) {
        *interval_seconds = seconds;
        *due_ns = now + interval;
        return false;
    }
    if (now < *due_ns) return false;
    *due_ns = now + interval;
    return true;
}

static bool random_behavior_candidate(BongoCatApp *app,
    const BongoCatBehaviorEntry *entry, BongoCatBehaviorKind kind) {
    return entry->kind == kind && (kind != BONGO_CAT_BEHAVIOR_MOTION ||
        bongo_cat_live2d_motion_visible(app->live2d, entry->group, entry->index));
}

static void random_behavior_run(BongoCatApp *app, uint64_t now,
    BongoCatBehaviorKind kind) {
    int current_expression = bongo_cat_live2d_expression(app->live2d);
    size_t count = 0;
    size_t alternate_count = 0;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (!random_behavior_candidate(app, entry, kind)) continue;
        count++;
        if (entry->index != current_expression) alternate_count++;
    }
    if (!count) return;

    /* Expressions prefer a different face. Motions must remain eligible when
       selected: triggering a persistent motion again switches it off. */
    bool choose_alternate = kind == BONGO_CAT_BEHAVIOR_EXPRESSION &&
        alternate_count > 0;
    size_t candidate_count = choose_alternate ? alternate_count : count;
    size_t choice = random_behavior_next(app, now) % candidate_count;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (!random_behavior_candidate(app, entry, kind) ||
            (choose_alternate && entry->index == current_expression)) continue;
        if (choice--) continue;
        if (kind == BONGO_CAT_BEHAVIOR_MOTION)
            bongo_cat_app_run_behavior(app, entry);
        else if (bongo_cat_live2d_set_expression(app->live2d, entry->index))
            app->dirty = true;
        return;
    }
}

void bongo_cat_random_behavior_update(BongoCatApp *app, uint64_t now) {
    if (app && app->window_snapshot) return;
    if (!app || !app->live2d) {
        bongo_cat_random_behavior_reset(app);
        return;
    }
    const BongoCatWindowPreferences *window = &app->settings.window;
    if (random_behavior_due(window->random_expression,
            window->random_expression_interval_seconds,
            BONGO_CAT_DEFAULT_RANDOM_EXPRESSION_SECONDS, now,
            &app->random_expression_due_ns,
            &app->random_expression_interval_seconds))
        random_behavior_run(app, now, BONGO_CAT_BEHAVIOR_EXPRESSION);
    if (random_behavior_due(window->random_motion,
            window->random_motion_interval_seconds,
            BONGO_CAT_DEFAULT_RANDOM_MOTION_SECONDS, now,
            &app->random_motion_due_ns, &app->random_motion_interval_seconds))
        random_behavior_run(app, now, BONGO_CAT_BEHAVIOR_MOTION);
}

