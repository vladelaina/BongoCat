#ifndef BONGO_CAT_WINDOW_SNAPSHOT_H
#define BONGO_CAT_WINDOW_SNAPSHOT_H

#include "bongo_cat/app.h"
#include <SDL3/SDL.h>

/* Begin also refreshes the idle deadline of an existing preview. */
void bongo_cat_window_snapshot_begin(BongoCatApp *app);
/* Draw the final live frame before releasing the preview. */
void bongo_cat_window_snapshot_end(BongoCatApp *app);
/* Release without rendering, for hiding, shutdown or presentation failure. */
void bongo_cat_window_snapshot_discard(BongoCatApp *app);
void bongo_cat_window_snapshot_update(BongoCatApp *app, uint64_t now);
void bongo_cat_window_snapshot_present(BongoCatApp *app);
/* NULL follows the native window rectangle (drag); otherwise use subpixels. */
void bongo_cat_window_snapshot_geometry(BongoCatApp *app, const SDL_FRect *rect);
bool bongo_cat_window_snapshot_hit(BongoCatApp *app, float x, float y);

#endif
