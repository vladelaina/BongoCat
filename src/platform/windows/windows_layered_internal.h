#ifndef BONGO_CAT_WINDOWS_LAYERED_INTERNAL_H
#define BONGO_CAT_WINDOWS_LAYERED_INTERNAL_H

#include "windows_layered.h"

#ifdef _WIN32
#include <windows.h>

typedef struct BongoCatWindowsLayered {
    HDC memory_dc;
    HBITMAP bitmap;
    HGDIOBJ original_bitmap;
    unsigned char *pixels;
    unsigned char *readback;
    size_t readback_capacity;
    HWND proxy;
    HWND source;
    int width, height, source_width, source_height;
    bool readback_valid, active, has_frame, mode_logged;
    bool source_transparent, visible, topmost;
    BYTE applied_alpha;
    bool applied_alpha_valid;
    bool forced, hdr_failed, bound, syncing;
    bool source_destroyed;
    LONG_PTR source_style;
} BongoCatWindowsLayered;

bool bongo_cat_windows_layered_update_proxy(BongoCatPlatform *platform,
    BongoCatWindowsLayered *value);
bool bongo_cat_windows_layered_bind(BongoCatPlatform *platform);
void bongo_cat_windows_layered_unbind(BongoCatPlatform *platform);
LRESULT CALLBACK bongo_cat_windows_layered_window_proc(HWND window,
    UINT message, WPARAM wparam, LPARAM lparam);
void bongo_cat_windows_layered_sync_input(BongoCatPlatform *platform);
#endif

#endif
