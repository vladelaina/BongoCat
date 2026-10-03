#ifndef BONGO_CAT_UI_PAINT_CACHE_H
#define BONGO_CAT_UI_PAINT_CACHE_H

#include "ui_backend.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum BongoCatUIPaintKind {
    BONGO_CAT_UI_PAINT_GRADIENT = 1,
    BONGO_CAT_UI_PAINT_RADIAL = 2,
    BONGO_CAT_UI_PAINT_SHADOW = 3,
    BONGO_CAT_UI_PAINT_RADIAL_CIRCLE = 4,
    BONGO_CAT_UI_PAINT_DASHED_ROUNDED = 5,
    BONGO_CAT_UI_PAINT_SIDEBAR_GLOW = 6,
    BONGO_CAT_UI_PAINT_ROUNDED_SURFACE = 7
} BongoCatUIPaintKind;

typedef struct BongoCatUIPaintKey {
    int kind, width, height, radius;
    int first_parameter, second_parameter;
    uint32_t first_color, second_color;
} BongoCatUIPaintKey;

typedef struct BongoCatUIPaintTexture BongoCatUIPaintTexture;

BongoCatUIPaintTexture *bongo_cat_ui_paint_cache_get(
    BongoCatUIBackend *backend, const BongoCatUIPaintKey *key);
bool bongo_cat_ui_paint_cache_ready(
    const BongoCatUIPaintTexture *item);
bool bongo_cat_ui_paint_cache_upload(BongoCatUIPaintTexture *item,
    const unsigned char *pixels, bool single_channel);
void bongo_cat_ui_paint_cache_draw(struct nk_context *context,
    struct nk_rect bounds, const BongoCatUIPaintTexture *item,
    struct nk_color tint);
void bongo_cat_ui_paint_cache_begin_frame(BongoCatUIBackend *backend);
void bongo_cat_ui_paint_cache_trim_idle(BongoCatUIBackend *backend);
size_t bongo_cat_ui_paint_cache_usage(BongoCatUIBackend *backend,
    size_t *texture_count);
void bongo_cat_ui_paint_cache_destroy(BongoCatUIBackend *backend);
void bongo_cat_ui_paint_cache_abandon(BongoCatUIBackend *backend);

#endif
