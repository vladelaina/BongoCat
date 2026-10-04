#include "linux_shape.h"
#include "../common/gl_readback.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool apply(BongoCatPlatform *platform, const unsigned char *mask,
    int width, int height, bool empty, bool commit) {
    LinuxInputShape *shape = platform->presenter;
    return shape->wayland ? bongo_cat_linux_wayland_shape(shape->wayland,
        platform->window, mask, width, height, empty, commit) :
        bongo_cat_linux_x11_shape(platform, mask, width, height, empty);
}

void bongo_cat_linux_shape_init(BongoCatPlatform *platform) {
    LinuxInputShape *shape = calloc(1, sizeof(*shape));
    if (!shape) return;
    platform->presenter = shape;
    shape->available = bongo_cat_linux_x11_shape_supported(platform);
    if (!shape->available) {
        shape->wayland = bongo_cat_linux_wayland_shape_create(platform->window);
        shape->available = shape->wayland != NULL;
    }
    if (!shape->available) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "Native transparent-pixel input regions are unavailable for this Linux backend");
}

void bongo_cat_linux_shape_destroy(BongoCatPlatform *platform) {
    LinuxInputShape *shape = platform ? platform->presenter : NULL;
    if (!shape) return;
    bongo_cat_linux_wayland_shape_destroy(shape->wayland);
    free(shape->rgba);
    free(shape->mask);
    free(shape);
    platform->presenter = NULL;
}

static bool reserve(unsigned char **data, size_t *capacity, size_t bytes) {
    if (*capacity >= bytes && *capacity / 2 <= bytes) return true;
    unsigned char *next = realloc(*data, bytes);
    if (!next) return *capacity >= bytes;
    *data = next;
    *capacity = bytes;
    return true;
}

static void failure(BongoCatPlatform *platform) {
    LinuxInputShape *shape = platform->presenter;
    shape->valid = shape->applied = false;
    /* Restore interaction after a resize/readback failure. An old empty region
       must never strand a model which has moved to another part of its canvas. */
    apply(platform, NULL, 0, 0, shape->forced, true);
    if (!shape->warned) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "Cannot update the pet input region: %s", SDL_GetError());
    shape->warned = true;
}

void bongo_cat_linux_shape_reset(BongoCatPlatform *platform) {
    LinuxInputShape *shape = platform ? platform->presenter : NULL;
    if (shape && shape->available) failure(platform);
}

void bongo_cat_linux_shape_capture(BongoCatPlatform *platform, int width, int height) {
    LinuxInputShape *shape = platform ? platform->presenter : NULL;
    if (!shape || !shape->available ||
        !(SDL_GetWindowFlags(platform->window) & SDL_WINDOW_TRANSPARENT)) return;
    int logical_width, logical_height;
    if (!SDL_GetWindowSize(platform->window, &logical_width, &logical_height) ||
        width <= 0 || height <= 0 || width > INT_MAX / 4 ||
        logical_width <= 0 || logical_height <= 0 ||
        logical_width > 32767 || logical_height > 32767 ||
        (size_t)height > SIZE_MAX / ((size_t)width * 4)) {
        failure(platform); return;
    }
    size_t bytes = (size_t)width * height * 4;
    size_t stride = ((size_t)logical_width + 7) / 8;
    size_t mask_bytes = stride * logical_height;
    bool changed = !shape->valid || shape->width != logical_width ||
        shape->height != logical_height;
    if (!reserve(&shape->rgba, &shape->rgba_capacity, bytes) ||
        !reserve(&shape->mask, &shape->mask_capacity, mask_bytes) ||
        !bongo_cat_gl_read_window(0, 0, width, height, true, shape->rgba)) {
        failure(platform); return;
    }
    /* Keep only one bit per logical pixel in the input mask. The RGBA frame is
       reused for readback and hover/drag sampling, with no SDL surface copy.
       A logical pixel is interactive if any covered physical pixel is visible. */
    for (int y = 0; y < logical_height; ++y) {
        int y0 = (int)((int64_t)y * height / logical_height);
        int y1 = (int)(((int64_t)(y + 1) * height + logical_height - 1) / logical_height);
        for (size_t column = 0; column < stride; ++column) {
            unsigned char bits = 0;
            for (int bit = 0; bit < 8; ++bit) {
                int x = (int)(column * 8) + bit;
                if (x >= logical_width) break;
                int x0 = (int)((int64_t)x * width / logical_width);
                int x1 = (int)(((int64_t)(x + 1) * width + logical_width - 1) / logical_width);
                bool visible = false;
                for (int py = y0; py < y1 && !visible; ++py)
                    for (int px = x0; px < x1; ++px)
                        if (shape->rgba[((size_t)(height - 1 - py) * width + px) * 4 + 3] > 8) {
                            visible = true; break;
                        }
                if (visible) bits |= (unsigned char)(1u << bit);
            }
            size_t offset = (size_t)y * stride + column;
            if (!changed && shape->mask[offset] != bits) changed = true;
            shape->mask[offset] = bits;
        }
    }
    shape->width = logical_width; shape->height = logical_height;
    shape->pixel_width = width; shape->pixel_height = height;
    shape->valid = true;
    if (!shape->applied || (changed && !shape->forced)) {
        shape->applied = apply(platform, shape->mask, logical_width,
            logical_height, shape->forced, false);
        if (!shape->applied) { failure(platform); return; }
    }
    shape->warned = false;
}

void bongo_cat_linux_shape_force(BongoCatPlatform *platform, bool forced) {
    LinuxInputShape *shape = platform ? platform->presenter : NULL;
    if (!shape || !shape->available) return;
    if (shape->forced == forced && shape->applied) return;
    shape->forced = forced;
    shape->applied = apply(platform, shape->valid ? shape->mask : NULL,
        shape->width, shape->height, forced, true);
}

bool bongo_cat_linux_shape_alpha(const BongoCatPlatform *platform,
    int width, int height, int x, int y, uint8_t *alpha) {
    const LinuxInputShape *shape = platform ? platform->presenter : NULL;
    if (!shape || !shape->valid || !alpha ||
        width != shape->pixel_width || height != shape->pixel_height ||
        x < 0 || y < 0 || x >= width || y >= height) return false;
    *alpha = shape->rgba[((size_t)y * width + x) * 4 + 3];
    return true;
}
