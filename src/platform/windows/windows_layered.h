#ifndef BONGO_CAT_WINDOWS_LAYERED_H
#define BONGO_CAT_WINDOWS_LAYERED_H

#include "bongo_cat/platform.h"

#ifdef _WIN32
#include <windows.h>
/* Transparent pets use native per-pixel input; UI only needs HDR presentation. */
void *bongo_cat_windows_layered_create(bool pixel_hit_test);
void bongo_cat_windows_layered_destroy(BongoCatPlatform *platform);
bool bongo_cat_windows_layered_native_hit_test(const BongoCatPlatform *platform);
void bongo_cat_windows_layered_set_click_through(
    BongoCatPlatform *platform, bool enabled);
void bongo_cat_windows_layered_set_always_on_top(
    BongoCatPlatform *platform, bool enabled);
HWND bongo_cat_windows_layered_proxy(HWND source);
bool bongo_cat_windows_layered_suppressed(HWND source);
#endif

#endif
