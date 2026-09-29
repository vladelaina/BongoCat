#ifndef BONGO_CAT_UI_PRESENT_H
#define BONGO_CAT_UI_PRESENT_H

#include <SDL3/SDL.h>

/* SDL swaps remain active; Windows HDR adds a per-pixel-alpha presentation. */
bool bongo_cat_ui_present(SDL_Window *window);
#endif
