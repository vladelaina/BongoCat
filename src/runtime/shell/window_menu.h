#ifndef BONGO_CAT_WINDOW_MENU_INTERNAL_H
#define BONGO_CAT_WINDOW_MENU_INTERNAL_H

#include "modal_frame.h"

typedef struct BongoCatWindowMenuPreview {
    BongoCatApp *app;
    float scale, opacity;
    int expression;
    BongoCatMenuAction last, applied;
    BongoCatModalFrame modal_frame;
} BongoCatWindowMenuPreview;

void bongo_cat_window_menu_preview_init(BongoCatWindowMenuPreview *state,
    BongoCatApp *app);
void bongo_cat_window_menu_preview(void *userdata, BongoCatMenuAction action);
void bongo_cat_window_menu_preview_tick(void *userdata);
void bongo_cat_window_menu_restore(void *userdata, BongoCatMenuAction selected);
bool bongo_cat_window_menu_preview_applied(
    const BongoCatWindowMenuPreview *state, BongoCatMenuAction selected);
void bongo_cat_window_audio_labels(BongoCatApp *app,
    char names[][BONGO_CAT_MENU_LABEL_CAP], bool *checked, size_t *count);
void bongo_cat_window_behavior_labels(BongoCatApp *app,
    char motions[][BONGO_CAT_MENU_LABEL_CAP], bool *motion_checked,
    size_t *motion_count, char expressions[][BONGO_CAT_MENU_LABEL_CAP],
    size_t *expression_count,
    size_t *current_expression);
bool bongo_cat_window_behavior_menu_action(BongoCatMenuAction action);
bool bongo_cat_window_behavior_action(BongoCatApp *app,
    BongoCatMenuAction action);
bool bongo_cat_window_behavior_preview(BongoCatApp *app,
    BongoCatMenuAction action);
bool bongo_cat_window_behavior_commit_preview(BongoCatApp *app,
    BongoCatMenuAction action);
bool bongo_cat_window_behavior_self_test(BongoCatApp *app);

#endif
