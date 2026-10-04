#ifndef BONGO_CAT_LINUX_SHAPE_H
#define BONGO_CAT_LINUX_SHAPE_H

#include "bongo_cat/platform.h"

typedef struct LinuxInputShape {
    unsigned char *rgba, *mask;
    size_t rgba_capacity, mask_capacity;
    int pixel_width, pixel_height, width, height;
    bool valid, applied, forced, available, warned;
    void *wayland;
} LinuxInputShape;

void bongo_cat_linux_shape_init(BongoCatPlatform *platform);
void bongo_cat_linux_shape_destroy(BongoCatPlatform *platform);
void bongo_cat_linux_shape_capture(BongoCatPlatform *platform, int width, int height);
void bongo_cat_linux_shape_reset(BongoCatPlatform *platform);
void bongo_cat_linux_shape_force(BongoCatPlatform *platform, bool forced);
bool bongo_cat_linux_shape_alpha(const BongoCatPlatform *platform,
    int width, int height, int x, int y, uint8_t *alpha);
bool bongo_cat_linux_x11_shape_supported(BongoCatPlatform *platform);
bool bongo_cat_linux_x11_shape(BongoCatPlatform *platform,
    const unsigned char *mask, int width, int height, bool empty);
void *bongo_cat_linux_wayland_shape_create(SDL_Window *window);
void bongo_cat_linux_wayland_shape_destroy(void *state);
bool bongo_cat_linux_wayland_shape(void *state, SDL_Window *window,
    const unsigned char *mask, int width, int height, bool empty, bool commit);

#endif
