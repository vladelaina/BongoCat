#ifndef BONGO_CAT_MVER_RENDER_H
#define BONGO_CAT_MVER_RENDER_H
#include "bongo_cat/model.h"
#include <yyjson.h>
void bongo_cat_mver_render_options(yyjson_val *config,
    BongoCatLive2DRenderOptions *options);
bool bongo_cat_mver_render_read(const char *path,
    BongoCatLive2DRenderOptions *options);
#endif
