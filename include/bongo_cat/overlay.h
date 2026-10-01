#ifndef BONGO_CAT_OVERLAY_H
#define BONGO_CAT_OVERLAY_H

#include "bongo_cat/model.h"

typedef struct BongoCatOverlay BongoCatOverlay;

BongoCatOverlay *bongo_cat_overlay_create(BongoCatError *error);
void bongo_cat_overlay_destroy(BongoCatOverlay *overlay);
void bongo_cat_overlay_clear(BongoCatOverlay *overlay);
BongoCatResult bongo_cat_overlay_load(BongoCatOverlay *overlay,
    const char *model_directory, bool model_pointer_preferred,
    const BongoCatLive2DRenderOptions *render_options, BongoCatError *error);
int bongo_cat_overlay_key(BongoCatOverlay *overlay, const char *name, bool pressed);
bool bongo_cat_overlay_effect(BongoCatOverlay *overlay, const char *path);
bool bongo_cat_overlay_hand_active(const BongoCatOverlay *overlay, bool right);
bool bongo_cat_overlay_mver_pointer_enabled(const BongoCatOverlay *overlay);
bool bongo_cat_overlay_mver_pointer_left_handed(const BongoCatOverlay *overlay);
void bongo_cat_overlay_set_mver_pointer(BongoCatOverlay *overlay,
    float x_ratio, float y_ratio, bool left, bool right, bool side);
void bongo_cat_overlay_draw_background(BongoCatOverlay *overlay, bool mirror);
void bongo_cat_overlay_draw_pointer_before_keys(BongoCatOverlay *overlay);
void bongo_cat_overlay_draw_keys(BongoCatOverlay *overlay, bool mirror);
void bongo_cat_overlay_draw_effect(BongoCatOverlay *overlay, bool mirror);
void bongo_cat_overlay_draw_pointer_after_keys(BongoCatOverlay *overlay);

#endif
