#ifndef BONGO_CAT_PREFERENCES_MODEL_COVER_H
#define BONGO_CAT_PREFERENCES_MODEL_COVER_H

#include "bongo_cat/model.h"

typedef struct BongoCatApp BongoCatApp;

typedef struct BongoCatModelCover {
    unsigned int texture;
    int width;
    int height;
} BongoCatModelCover;

void bongo_cat_preferences_model_covers_begin(BongoCatApp *app);
void bongo_cat_preferences_model_cover_cache_clear(BongoCatApp *app);
bool bongo_cat_preferences_model_cover_reload(BongoCatApp *app,
    const char *path);
void bongo_cat_preferences_model_cache_abandon(BongoCatApp *app);
const BongoCatModelCover *bongo_cat_preferences_model_cover(
    BongoCatApp *app, const BongoCatModelEntry *entry,
    int pixel_width, int pixel_height);
void bongo_cat_preferences_model_covers_prune(BongoCatApp *app);
size_t bongo_cat_preferences_model_cover_usage(BongoCatApp *app, size_t *count);

#endif
