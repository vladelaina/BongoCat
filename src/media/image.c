#include "bongo_cat/image.h"
#include "image_internal.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stb_image.h>
#include <stdlib.h>
#include <string.h>

BongoCatResult bongo_cat_image_load(const char *path, BongoCatImage *image, BongoCatError *error) {
    BongoCatResult result = bongo_cat_image_decode_pixels(path, image, error);
    if (result != BONGO_CAT_OK) return result;
    image->surface = SDL_CreateSurfaceFrom(image->width, image->height,
        SDL_PIXELFORMAT_RGBA32, image->pixels, image->width * 4);
    if (!image->surface) {
        bongo_cat_image_free(image);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM, "Cannot create image surface");
        return BONGO_CAT_ERROR_PLATFORM;
    }
    return BONGO_CAT_OK;
}
void bongo_cat_image_free(BongoCatImage *image) {
    if (!image) return;
    if (image->surface) SDL_DestroySurface(image->surface);
    if (image->pixels) {
        if (image->pixels_stbi) stbi_image_free(image->pixels);
        else free(image->pixels);
    }
    memset(image, 0, sizeof(*image));
}
unsigned int bongo_cat_image_texture(const char *path, int *width, int *height, BongoCatError *error) {
    BongoCatImage image;
    if (bongo_cat_image_load(path, &image, error) != BONGO_CAT_OK) return 0;
    GLuint texture = bongo_cat_image_upload_texture(&image, 0, false, error);
    if (width) *width = image.width;
    if (height) *height = image.height;
    bongo_cat_image_free(&image);
    return texture;
}

unsigned int bongo_cat_image_texture_thumbnail(const char *path, int max_width,
    int max_height, int *width, int *height, BongoCatError *error) {
    BongoCatImage image;
#ifdef _WIN32
    if (max_width > 0 && max_height > 0 &&
        bongo_cat_image_decode_wic_responsive(path, &image,
            max_width, max_height, NULL, NULL)) {
        GLuint texture = bongo_cat_image_upload_texture(&image, 0, false, error);
        if (width) *width = image.width;
        if (height) *height = image.height;
        bongo_cat_image_free(&image);
        return texture;
    }
#endif
    if (bongo_cat_image_load(path, &image, error) != BONGO_CAT_OK) return 0;
    int target_width = image.width, target_height = image.height;
    if (max_width > 0 && max_height > 0 &&
        (target_width > max_width || target_height > max_height)) {
        float scale = SDL_min((float)max_width / target_width,
            (float)max_height / target_height);
        target_width = SDL_max(1, (int)(target_width * scale + .5f));
        target_height = SDL_max(1, (int)(target_height * scale + .5f));
        SDL_Surface *scaled = SDL_ScaleSurface(image.surface, target_width,
            target_height, SDL_SCALEMODE_LINEAR);
        if (scaled) {
            BongoCatImage thumbnail = {
                .pixels = scaled->pixels, .width = scaled->w, .height = scaled->h};
            GLuint texture = bongo_cat_image_upload_texture(&thumbnail, 0, false, error);
            if (width) *width = thumbnail.width;
            if (height) *height = thumbnail.height;
            SDL_DestroySurface(scaled);
            bongo_cat_image_free(&image);
            return texture;
        }
        target_width = image.width;
        target_height = image.height;
    }
    GLuint texture = bongo_cat_image_upload_texture(&image, 0, false, error);
    if (width) *width = target_width;
    if (height) *height = target_height;
    bongo_cat_image_free(&image);
    return texture;
}
static void erase_paw(BongoCatImage *image, bool left) {
    float cx = left ? .700f : .275f, cy = left ? .515f : .397f;
    float rx = left ? .080f : .070f, ry = left ? .170f : .160f;
    for (int y = 0; y < image->height; ++y) for (int x = 0; x < image->width; ++x) {
        float dx = (x / (float)image->width - cx) / rx;
        float dy = (y / (float)image->height - cy) / ry;
        unsigned char *pixel = image->pixels + ((size_t)y * image->width + x) * 4;
        if (dx * dx + dy * dy < 1.0f && pixel[3])
            pixel[0] = pixel[1] = pixel[2] = 255;
    }
}

static bool blend_file(BongoCatImage *base, const char *path, BongoCatError *error) {
    if (!path || !path[0]) return true;
    BongoCatImage layer;
    if (bongo_cat_image_load(path, &layer, error) != BONGO_CAT_OK) return false;
    bool valid = layer.width == base->width && layer.height == base->height;
    if (valid) for (int i = 0; i < base->width * base->height; ++i) {
        unsigned char *dst = base->pixels + i * 4, *src = layer.pixels + i * 4;
        unsigned alpha = src[3], inverse = 255 - alpha, destination_alpha = dst[3];
        unsigned output_alpha = alpha * 255 + destination_alpha * inverse;
        if (!output_alpha) { memset(dst, 0, 4); continue; }
        for (int channel = 0; channel < 3; ++channel) {
            unsigned color = src[channel] * alpha * 255 +
                dst[channel] * destination_alpha * inverse;
            dst[channel] = (unsigned char)((color + output_alpha / 2) / output_alpha);
        }
        dst[3] = (unsigned char)((output_alpha + 127) / 255);
    }
    bongo_cat_image_free(&layer);
    return valid;
}

unsigned int bongo_cat_image_composite_texture(const char *base, const char *left,
    const char *right, unsigned int texture, bool erase_left, bool erase_right,
    BongoCatError *error) {
    BongoCatImage image;
    if (bongo_cat_image_load(base, &image, error) != BONGO_CAT_OK) return 0;
    if (erase_left) erase_paw(&image, true);
    if (erase_right) erase_paw(&image, false);
    bool valid = blend_file(&image, left, error) && blend_file(&image, right, error);
    if (valid) {
        GLuint updated = bongo_cat_image_upload_texture(&image, texture, false, error);
        if (updated) texture = updated;
    }
    bongo_cat_image_free(&image);
    return texture;
}
