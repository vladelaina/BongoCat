#ifndef BONGO_CAT_UI_NATIVE_THEME_H
#define BONGO_CAT_UI_NATIVE_THEME_H

#include <stdbool.h>

typedef struct SDL_Window SDL_Window;

void bongo_cat_ui_native_theme_apply(SDL_Window *window, bool dark);
void bongo_cat_ui_native_menu_prepare(SDL_Window *window, bool dark);
void bongo_cat_ui_native_menu_prepare_native(void *handle, bool dark);

#endif
