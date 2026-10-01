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
    int width, height, source_width, source_height;
    bool readback_valid, active, has_frame, mode_logged;
    bool source_transparent, visible, topmost;
    BYTE applied_alpha;
    bool applied_alpha_valid;
} BongoCatWindowsLayered;

bool bongo_cat_windows_layered_update_proxy(BongoCatPlatform *platform,
    BongoCatWindowsLayered *value);
#endif

#endif
