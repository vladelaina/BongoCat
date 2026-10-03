#ifndef BONGO_CAT_WINDOWS_HDR_H
#define BONGO_CAT_WINDOWS_HDR_H

#include <SDL3/SDL.h>

/* HDR affects desktop composition even though the application draws SDR. */
bool bongo_cat_windows_hdr_enabled(SDL_Window *window);
/* Returns -1 if Windows cannot report the current monitor's color state. */
int bongo_cat_windows_display_hdr(SDL_Window *window);
void bongo_cat_windows_prepare_transparent_ui(SDL_Window *window);
bool bongo_cat_windows_hdr_present(SDL_Window *window, int width, int height);
#endif
