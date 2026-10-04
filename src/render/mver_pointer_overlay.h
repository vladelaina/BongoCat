#ifndef BONGO_CAT_MVER_POINTER_OVERLAY_H
#define BONGO_CAT_MVER_POINTER_OVERLAY_H

#include "bongo_cat/common.h"

typedef struct BongoCatMverPointerOverlay BongoCatMverPointerOverlay;

BongoCatMverPointerOverlay *bongo_cat_mver_pointer_overlay_create(
    BongoCatError *error);
void bongo_cat_mver_pointer_overlay_destroy(BongoCatMverPointerOverlay *value);
void bongo_cat_mver_pointer_overlay_clear(BongoCatMverPointerOverlay *value);
bool bongo_cat_mver_pointer_overlay_load(BongoCatMverPointerOverlay *value,
    const char *directory, BongoCatError *error);
bool bongo_cat_mver_pointer_overlay_enabled(
    const BongoCatMverPointerOverlay *value);
bool bongo_cat_mver_pointer_overlay_left_handed(
    const BongoCatMverPointerOverlay *value);
void bongo_cat_mver_pointer_overlay_set_vertical_flip(
    BongoCatMverPointerOverlay *value, bool flipped);
void bongo_cat_mver_pointer_overlay_set(BongoCatMverPointerOverlay *value,
    float x_ratio, float y_ratio, bool left, bool right, bool side);
void bongo_cat_mver_pointer_overlay_draw_before_keys(
    BongoCatMverPointerOverlay *value);
void bongo_cat_mver_pointer_overlay_draw_after_keys(
    BongoCatMverPointerOverlay *value);

#endif
