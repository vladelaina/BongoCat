#ifndef BONGO_CAT_WINDOWS_SNAPSHOT_INTERNAL_H
#define BONGO_CAT_WINDOWS_SNAPSHOT_INTERNAL_H

#include "windows_snapshot.h"
#include <windows.h>

#define SNAPSHOT_MAX_PIXELS (16u * 1024u * 1024u)

struct BongoCatWindowsSnapshot {
    SDL_Window *source, *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    HWND source_handle, handle;
    WNDPROC original;
    /* RGBA during capture; packed 8-bit alpha after the texture upload. */
    unsigned char *pixels;
    size_t pixel_bytes;
    bool ready;
    int width, height;
    RECT desktop;
    SDL_FRect destination, presented;
    float opacity;
    bool suppressed, has_frame, custom_destination;
};

bool bongo_cat_windows_snapshot_capture(BongoCatWindowsSnapshot *snapshot);
bool bongo_cat_windows_snapshot_bind_input(BongoCatWindowsSnapshot *snapshot);
void bongo_cat_windows_snapshot_unbind_input(BongoCatWindowsSnapshot *snapshot);

#endif
