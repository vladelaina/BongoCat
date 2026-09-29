#ifndef BONGO_CAT_IMAGE_H
#define BONGO_CAT_IMAGE_H

#include "bongo_cat/common.h"

typedef struct SDL_Surface SDL_Surface;

typedef struct BongoCatImage {
    unsigned char *pixels;
    int width;
    int height;
    SDL_Surface *surface;
    bool pixels_stbi;
} BongoCatImage;

#define BONGO_CAT_ALPHA_MASK_SIZE 128
typedef struct BongoCatImageAlphaMask {
    unsigned char pixels[BONGO_CAT_ALPHA_MASK_SIZE * BONGO_CAT_ALPHA_MASK_SIZE];
    int width, height;
} BongoCatImageAlphaMask;
typedef void (*BongoCatImageProgress)(void *userdata, float progress);

#ifdef __cplusplus
extern "C" {
#endif

BongoCatResult bongo_cat_image_load(const char *path, BongoCatImage *image, BongoCatError *error);
bool bongo_cat_image_info(const char *path, int *width, int *height);
void bongo_cat_image_free(BongoCatImage *image);
unsigned int bongo_cat_image_texture(const char *path, int *width, int *height,
    BongoCatError *error);
unsigned int bongo_cat_image_texture_thumbnail(const char *path, int max_width,
    int max_height, int *width, int *height, BongoCatError *error);
unsigned int bongo_cat_image_texture_resampled(const char *path,
    int max_width, int max_height, float rounding, int *width, int *height,
    BongoCatError *error);
/* Preserves the source dimensions and texels in a premultiplied-alpha texture.
   Unsupported sizes or allocation failures are reported, never downsampled. */
unsigned int bongo_cat_image_texture_model(const char *path, bool direct_decode,
    int *width, int *height, BongoCatImageAlphaMask *alpha,
    BongoCatImageProgress progress, void *userdata, BongoCatError *error);
/* Upload a Live2D texture after bounded downsampling.  The original texture
   path is used automatically when the requested bounds do not require a
   resize, so callers can use one API for both cases. */
unsigned int bongo_cat_image_texture_model_scaled(const char *path,
    bool direct_decode, int max_width, int max_height, int *width, int *height,
    BongoCatImageAlphaMask *alpha, BongoCatImageProgress progress,
    void *userdata, BongoCatError *error);
/* Configure before starting model workers. NULL disables the optional cache. */
void bongo_cat_image_set_texture_cache_root(const char *root);
void bongo_cat_image_forget_texture_cache(const char *digest,
    int max_width, int max_height);
/* digest must identify the current source bytes (SHA-256, lowercase hex).
   NULL retains uncached loading. Cache failures never prevent source loading. */
unsigned int bongo_cat_image_texture_model_scaled_cached(const char *path,
    const char *digest, bool direct_decode, int max_width, int max_height,
    int *width, int *height, BongoCatImageAlphaMask *alpha,
    BongoCatImageProgress progress, void *userdata, BongoCatError *error);
void bongo_cat_image_make_alpha_mask(const BongoCatImage *image,
    BongoCatImageAlphaMask *mask);
unsigned int bongo_cat_image_composite_texture(const char *base, const char *left,
    const char *right, unsigned int texture, bool erase_left, bool erase_right,
    BongoCatError *error);

#ifdef __cplusplus
}
#endif

#endif
