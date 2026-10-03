#ifndef BONGO_CAT_WINDOWS_BORDERLESS_H
#define BONGO_CAT_WINDOWS_BORDERLESS_H

#ifdef _WIN32
#include "bongo_cat/platform.h"
#include <windows.h>

void bongo_cat_windows_borderless_install(HWND window);
void bongo_cat_windows_borderless_uninstall(HWND window);
bool bongo_cat_windows_borderless_preserve_screen(HWND window, bool enabled);
bool bongo_cat_windows_borderless_hit_transparent(bool forced,
    bool pointer_transparent, bool right_button_down);
void bongo_cat_windows_borderless_set_click_through(HWND window,
    bool forced, bool pointer_transparent);
void bongo_cat_windows_menu_preview(HWND window, BongoCatMenuPreview preview,
    void (*tick)(void *userdata), void *userdata);
void bongo_cat_windows_begin_drag(HWND window, BongoCatModalTick modal_tick,
    void *userdata);
#endif

#endif
