#include "image_internal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stdint.h>
#include <stdlib.h>

static unsigned char *next_level(const unsigned char *source,
    int source_width, int source_height, int *width, int *height) {
    *width = source_width > 1 ? source_width / 2 : 1;
    *height = source_height > 1 ? source_height / 2 : 1;
    size_t count = (size_t)*width * (size_t)*height;
    unsigned char *target = count <= SIZE_MAX / 4 ? malloc(count * 4) : NULL;
    if (!target) return NULL;
    for (int y = 0; y < *height; ++y) for (int x = 0; x < *width; ++x) {
        int sx = x * 2, sy = y * 2;
        int sx1 = SDL_min(sx + 1, source_width - 1);
        int sy1 = SDL_min(sy + 1, source_height - 1);
        const unsigned char *samples[] = {
            source + ((size_t)sy * source_width + sx) * 4,
            source + ((size_t)sy * source_width + sx1) * 4,
            source + ((size_t)sy1 * source_width + sx) * 4,
            source + ((size_t)sy1 * source_width + sx1) * 4};
        unsigned alpha = 0, red = 0, green = 0, blue = 0;
        for (size_t i = 0; i < 4; ++i) {
            unsigned sample_alpha = samples[i][3];
            alpha += sample_alpha;
            red += samples[i][0];
            green += samples[i][1];
            blue += samples[i][2];
        }
        unsigned char *pixel = target + ((size_t)y * *width + x) * 4;
        pixel[3] = (unsigned char)((alpha + 2) / 4);
        /* Source RGB already includes alpha; averaging it again with alpha
         * weights would darken translucent edges at every mip level. */
        pixel[0] = (unsigned char)((red + 2) / 4);
        pixel[1] = (unsigned char)((green + 2) / 4);
        pixel[2] = (unsigned char)((blue + 2) / 4);
    }
    return target;
}

bool bongo_cat_image_upload_mipmaps(const BongoCatImage *image) {
    if (!image || !image->pixels || image->width < 1 || image->height < 1)
        return false;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height,
        0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
    if (glGetError() != GL_NO_ERROR) return false;
    const unsigned char *source = image->pixels;
    unsigned char *owned = NULL;
    int source_width = image->width, source_height = image->height, level = 1;
    while (source_width > 1 || source_height > 1) {
        int width, height;
        unsigned char *target = next_level(source, source_width, source_height,
            &width, &height);
        if (!target) { free(owned); return false; }
        glTexImage2D(GL_TEXTURE_2D, level++, GL_RGBA8, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, target);
        if (glGetError() != GL_NO_ERROR) {
            free(target); free(owned); return false;
        }
        free(owned);
        owned = target;
        source = target; source_width = width; source_height = height;
    }
    free(owned);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    return glGetError() == GL_NO_ERROR;
}
