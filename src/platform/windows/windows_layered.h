#ifndef BONGO_CAT_WINDOWS_LAYERED_H
#define BONGO_CAT_WINDOWS_LAYERED_H

#include "bongo_cat/platform.h"

#ifdef _WIN32
#include <windows.h>
void *bongo_cat_windows_layered_create(void);
void bongo_cat_windows_layered_destroy(BongoCatPlatform *platform);
void bongo_cat_windows_layered_set_click_through(
    BongoCatPlatform *platform, bool enabled);
void bongo_cat_windows_layered_set_always_on_top(
    BongoCatPlatform *platform, bool enabled);
HWND bongo_cat_windows_layered_proxy(HWND source);
bool bongo_cat_windows_layered_suppressed(HWND source);
#endif

#endif
