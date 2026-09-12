#include "window_menu.h"

static const BongoCatModelEntry *menu_model(const BongoCatApp *app,
    BongoCatMenuAction action) {
    if (!app || action < BONGO_CAT_MENU_MODEL_FIRST ||
        action >= BONGO_CAT_MENU_MODEL_FIRST + BONGO_CAT_MODEL_CAP) return NULL;
    size_t index = (size_t)(action - BONGO_CAT_MENU_MODEL_FIRST);
    return index < app->models.count ? &app->models.entries[index] : NULL;
}

static bool previewable(BongoCatMenuAction action) {
    return (action >= BONGO_CAT_MENU_SCALE_50 &&
        action <= BONGO_CAT_MENU_OPACITY_100) ||
        bongo_cat_window_behavior_menu_action(action);
}

static int preview_group(BongoCatMenuAction action) {
    if (action >= BONGO_CAT_MENU_SCALE_50 &&
        action <= BONGO_CAT_MENU_SCALE_200) return 1;
    if (action >= BONGO_CAT_MENU_OPACITY_10 &&
        action <= BONGO_CAT_MENU_OPACITY_100) return 2;
    if (action >= BONGO_CAT_MENU_MOTION_FIRST &&
        action < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP) return 4;
    if (action >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
        action < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP) return 5;
    return 0;
}

void bongo_cat_window_menu_preview_init(BongoCatWindowMenuPreview *state,
    BongoCatApp *app) {
    if (!state) return;
    *state = (BongoCatWindowMenuPreview){.app = app,
        .scale = app ? app->session.window.scale_percent : 100.0f,
        .opacity = app ? app->session.window.opacity_percent : 100.0f,
        .expression = app ? bongo_cat_live2d_expression(app->live2d) : -1,
        .last = BONGO_CAT_MENU_NONE, .applied = BONGO_CAT_MENU_NONE};
    bongo_cat_modal_frame_init(&state->modal_frame, app);
}

void bongo_cat_window_menu_preview(void *userdata, BongoCatMenuAction action) {
    BongoCatWindowMenuPreview *state = userdata;
    if (!state || !state->app || action == state->last) return;
    BongoCatApp *app = state->app;
    int previous_group = preview_group(state->last);
    int next_group = preview_group(action);
    if (state->last != BONGO_CAT_MENU_NONE && previous_group != next_group)
        bongo_cat_window_menu_restore(state, BONGO_CAT_MENU_NONE);
    if (!previewable(action)) {
        if (previous_group == 0)
            bongo_cat_window_menu_restore(state, BONGO_CAT_MENU_NONE);
        state->last = action;
        return;
    }
    state->last = action;
    if (action >= BONGO_CAT_MENU_SCALE_50 &&
        action <= BONGO_CAT_MENU_SCALE_200)
        bongo_cat_window_set_scale(app,
            (float)(50 + 10 * (action - BONGO_CAT_MENU_SCALE_50)));
    else if (action >= BONGO_CAT_MENU_OPACITY_10 &&
        action <= BONGO_CAT_MENU_OPACITY_100) {
        app->session.window.opacity_percent =
            (float)(10 * (action - BONGO_CAT_MENU_OPACITY_10 + 1));
        bongo_cat_app_cancel_hover_fade(app);
        bongo_cat_platform_set_opacity(&app->platform,
            app->session.window.opacity_percent / 100.0f);
    } else if (action == state->applied &&
        bongo_cat_window_behavior_menu_action(action)) {
        bongo_cat_modal_frame_tick(&state->modal_frame);
        return;
    } else {
        if (next_group == 5 &&
            bongo_cat_live2d_expression(app->live2d) != state->expression)
            bongo_cat_live2d_set_expression(app->live2d, state->expression);
        if (!bongo_cat_window_behavior_preview(app, action)) {
            bongo_cat_app_render_now(app);
            return;
        }
        state->applied = action;
        bongo_cat_modal_frame_tick(&state->modal_frame);
        return;
    }
    bongo_cat_app_render_now(app);
}

void bongo_cat_window_menu_preview_tick(void *userdata) {
    BongoCatWindowMenuPreview *state = userdata;
    if (!state) return;
    bongo_cat_modal_frame_tick(&state->modal_frame);
}

void bongo_cat_window_menu_restore(void *userdata, BongoCatMenuAction selected) {
    BongoCatWindowMenuPreview *state = userdata;
    if (!state || !state->app) return;
    BongoCatApp *app = state->app;
    bool changed = false;
    bool keep_motion = selected >= BONGO_CAT_MENU_MOTION_FIRST &&
        selected < BONGO_CAT_MENU_MOTION_FIRST + BONGO_CAT_BEHAVIOR_CAP;
    bool committed_motion = keep_motion &&
        bongo_cat_window_behavior_commit_preview(app, selected);
    if (!committed_motion && bongo_cat_live2d_restore_motion_preview(app->live2d)) {
        state->applied = BONGO_CAT_MENU_NONE;
        changed = true;
    }
    if (committed_motion) { state->applied = selected; changed = true; }
    if (selected == BONGO_CAT_MENU_NONE)
        state->applied = BONGO_CAT_MENU_NONE;
    bool keep_model = menu_model(app, selected) != NULL;
    bool keep_scale = selected >= BONGO_CAT_MENU_SCALE_50 &&
        selected <= BONGO_CAT_MENU_SCALE_200;
    bool keep_opacity = selected >= BONGO_CAT_MENU_OPACITY_10 &&
        selected <= BONGO_CAT_MENU_OPACITY_100;
    /* Expressions belong to the model active when the menu opened. */
    bool keep_expression = keep_model ||
        (selected >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
        selected < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP);
    bool committed_expression = selected >= BONGO_CAT_MENU_EXPRESSION_FIRST &&
        selected < BONGO_CAT_MENU_EXPRESSION_FIRST + BONGO_CAT_BEHAVIOR_CAP;
    if (!keep_scale &&
        SDL_fabsf(app->session.window.scale_percent - state->scale) > .01f) {
        bongo_cat_window_set_scale(app, state->scale);
        changed = true;
    }
    if (!keep_opacity &&
        SDL_fabsf(app->session.window.opacity_percent - state->opacity) > .01f) {
        app->session.window.opacity_percent = state->opacity;
        bongo_cat_app_cancel_hover_fade(app);
        bongo_cat_platform_set_opacity(&app->platform,
            state->opacity / 100.0f);
        changed = true;
    }
    if (!keep_expression &&
        bongo_cat_live2d_expression(app->live2d) != state->expression &&
        bongo_cat_live2d_set_expression(app->live2d, state->expression)) {
        bongo_cat_app_step_live2d(app, 1.0f / 60.0f);
        changed = true;
    }
    if (committed_expression)
        bongo_cat_app_capture_behavior_state(app);
    if (changed) bongo_cat_app_render_now(app);
}

bool bongo_cat_window_menu_preview_applied(
    const BongoCatWindowMenuPreview *state, BongoCatMenuAction selected) {
    return state && bongo_cat_window_behavior_menu_action(selected) &&
        state->applied == selected;
}
