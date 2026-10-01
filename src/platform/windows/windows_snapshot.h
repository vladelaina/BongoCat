#ifndef BONGO_CAT_WINDOWS_SNAPSHOT_H
#define BONGO_CAT_WINDOWS_SNAPSHOT_H

#include <SDL3/SDL.h>

typedef struct BongoCatWindowsSnapshot BongoCatWindowsSnapshot;

/* Capture requires the source OpenGL context to be current. */
BongoCatWindowsSnapshot *bongo_cat_windows_snapshot_create(SDL_Window *source,
    float opacity);
bool bongo_cat_windows_snapshot_present(BongoCatWindowsSnapshot *snapshot);
void bongo_cat_windows_snapshot_geometry(BongoCatWindowsSnapshot *snapshot,
    const SDL_FRect *rect);
void bongo_cat_windows_snapshot_pointer(BongoCatWindowsSnapshot *snapshot);
bool bongo_cat_windows_snapshot_hit(BongoCatWindowsSnapshot *snapshot,
    float x, float y);
void bongo_cat_windows_snapshot_destroy(BongoCatWindowsSnapshot *snapshot);

#endif
