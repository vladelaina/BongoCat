#define STBIR_NO_SIMD
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include "bongo_cat/image.h"
#include "image_internal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static float rounded_distance(float x, float y, float width, float height,
    float radius) {
    radius = SDL_clamp(radius, 0.0f, SDL_min(width, height) * .5f);
    float qx = fabsf(x - width * .5f) - (width * .5f - radius);
    float qy = fabsf(y - height * .5f) - (height * .5f - radius);
    float ox = SDL_max(qx, 0.0f), oy = SDL_max(qy, 0.0f);
    return sqrtf(ox * ox + oy * oy) + SDL_min(SDL_max(qx, qy), 0.0f) - radius;
}

static void apply_rounding(unsigned char *pixels, int width, int height,
    float radius) {
    if (radius <= 0.0f) return;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float distance = rounded_distance(x + .5f, y + .5f,
                (float)width, (float)height, radius);
            float coverage = SDL_clamp(.5f - distance, 0.0f, 1.0f);
            unsigned char *alpha = pixels + ((size_t)y * width + x) * 4 + 3;
            *alpha = (unsigned char)(*alpha * coverage + .5f);
        }
    }
}

static GLuint upload(unsigned char *pixels, int width, int height,
    BongoCatError *error) {
    BongoCatImage image = {.pixels = pixels, .width = width, .height = height};
    return bongo_cat_image_upload_texture(&image, 0, false, error);
}

bool bongo_cat_image_resize_rgba_take(BongoCatImage *source,
    int max_width, int max_height, BongoCatImage *target,
    BongoCatError *error) {
    if (!source || !source->pixels || source->width < 1 || source->height < 1 ||
        max_width < 1 || max_height < 1 || !target) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Invalid image resize request");
        return false;
    }
    if (target == source || source->width > INT_MAX / 4 ||
        source->height > INT_MAX / 4 || max_width > INT_MAX / 4 ||
        max_height > INT_MAX / 4) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Image dimensions are not supported for RGBA resize");
        return false;
    }
    *target = (BongoCatImage){0};
    if (source->width <= max_width && source->height <= max_height) {
        *target = *source;
        *source = (BongoCatImage){0};
        return true;
    }
    /* Use the same integer aspect fit as PNG/WIC and refresh scheduling.
       Float rounding could differ by a pixel and request the same job again. */
    int width, height;
    if ((int64_t)max_width * source->height <= (int64_t)max_height * source->width) {
        width = max_width;
        height = SDL_max(1, (int)((int64_t)source->height * max_width / source->width));
    } else {
        width = SDL_max(1, (int)((int64_t)source->width * max_height / source->height));
        height = max_height;
    }
    if ((size_t)width > SIZE_MAX / (size_t)height) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Image is too large to resize");
        return false;
    }
    size_t count = (size_t)width * (size_t)height;
    unsigned char *pixels = count <= SIZE_MAX / 4 ? malloc(count * 4) : NULL;
    if (!pixels || !stbir_resize_uint8_srgb(source->pixels,
        source->width, source->height, source->width * 4, pixels,
        width, height, width * 4, STBIR_RGBA)) {
        free(pixels);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot resize image to %dx%d", width, height);
        return false;
    }
    target->pixels = pixels;
    target->width = width;
    target->height = height;
    target->pixels_stbi = false;
    bongo_cat_image_free(source);
    return true;
}

unsigned int bongo_cat_image_texture_resampled(const char *path,
    int max_width, int max_height, float rounding, int *width, int *height,
    BongoCatError *error) {
    if (!path || max_width < 1 || max_height < 1)
        return 0;
    BongoCatImage source;
    if (bongo_cat_image_load(path, &source, error) != BONGO_CAT_OK)
        return 0;
    float scale = SDL_min((float)max_width / source.width,
        (float)max_height / source.height);
    int target_width = SDL_max(1, (int)lroundf(source.width * scale));
    int target_height = SDL_max(1, (int)lroundf(source.height * scale));
    size_t count = (size_t)target_width * target_height;
    unsigned char *pixels = count <= SIZE_MAX / 4 ? malloc(count * 4) : NULL;
    if (!pixels || !stbir_resize_uint8_srgb(source.pixels,
        source.width, source.height, source.width * 4, pixels,
        target_width, target_height, target_width * 4, STBIR_RGBA)) {
        free(pixels);
        bongo_cat_image_free(&source);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot resize image: %s", path);
        return 0;
    }
    bongo_cat_image_free(&source);
    apply_rounding(pixels, target_width, target_height,
        rounding * SDL_min((float)target_width / max_width,
            (float)target_height / max_height));
    GLuint texture = upload(pixels, target_width, target_height, error);
    free(pixels);
    if (width) *width = target_width;
    if (height) *height = target_height;
    return texture;
}
