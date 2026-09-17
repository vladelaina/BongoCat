#include "runtime.h"

#define NANOSECONDS_PER_SECOND 1000000000ull

static uint32_t random_expression_next(BongoCatApp *app, uint64_t now) {
    uint32_t state = app->random_expression_state;
    if (!state) {
        state = (uint32_t)now ^ (uint32_t)(now >> 32) ^ 0x9e3779b9u;
        const unsigned char *text = (const unsigned char *)app->loaded_model;
        while (*text) state = (state ^ *text++) * 16777619u;
        if (!state) state = 0x6d2b79f5u;
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    app->random_expression_state = state;
    return state;
}

void bongo_cat_random_expression_reset(BongoCatApp *app) {
    if (!app) return;
    app->random_expression_due_ns = 0;
    app->random_expression_interval_seconds = 0.0f;
}

void bongo_cat_random_expression_update(BongoCatApp *app, uint64_t now) {
    if (app && app->window_snapshot) return;
    if (!app || !app->settings.window.random_expression || !app->live2d) {
        bongo_cat_random_expression_reset(app);
        return;
    }
    float seconds = app->settings.window.random_expression_interval_seconds;
    if (!(seconds >= 1.0f && seconds <= 3600.0f))
        seconds = BONGO_CAT_DEFAULT_RANDOM_EXPRESSION_SECONDS;
    if (!app->random_expression_due_ns ||
        app->random_expression_interval_seconds != seconds) {
        app->random_expression_interval_seconds = seconds;
        app->random_expression_due_ns = now +
            (uint64_t)(seconds * (float)NANOSECONDS_PER_SECOND);
        return;
    }
    if (now < app->random_expression_due_ns) return;
    app->random_expression_due_ns = now +
        (uint64_t)(seconds * (float)NANOSECONDS_PER_SECOND);

    int current = bongo_cat_live2d_expression(app->live2d);
    size_t expression_count = 0;
    size_t alternate_count = 0;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != BONGO_CAT_BEHAVIOR_EXPRESSION) continue;
        expression_count++;
        if (entry->index != current) alternate_count++;
    }
    if (!expression_count) return;

    bool choose_alternate = alternate_count > 0;
    size_t candidate_count = choose_alternate ? alternate_count : expression_count;
    size_t choice = random_expression_next(app, now) % candidate_count;
    for (size_t i = 0; i < app->behaviors.count; ++i) {
        const BongoCatBehaviorEntry *entry = &app->behaviors.entries[i];
        if (entry->kind != BONGO_CAT_BEHAVIOR_EXPRESSION ||
            (choose_alternate && entry->index == current)) continue;
        if (choice--) continue;
        if (bongo_cat_live2d_set_expression(app->live2d, entry->index))
            app->dirty = true;
        return;
    }
}

bool bongo_cat_app_step_live2d(BongoCatApp *app, float elapsed_seconds) {
    if (app && app->window_snapshot) return false;
    if (!app || !app->live2d || elapsed_seconds <= 0.0f) return false;
    if (elapsed_seconds > 0.25f) elapsed_seconds = 0.25f;
    unsigned steps = 1;
    while (steps < 8 && elapsed_seconds / steps > 1.0f / 30.0f) steps++;
    float step = elapsed_seconds / steps;
    bool changed = false;
    for (unsigned i = 0; i < steps; ++i)
        changed = bongo_cat_live2d_update(app->live2d, step) || changed;
    if (changed) app->dirty = true;
    return changed;
}
