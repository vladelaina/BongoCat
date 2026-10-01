#ifndef BONGO_CAT_IMAGE_INTERNAL_H
#define BONGO_CAT_IMAGE_INTERNAL_H

#include "bongo_cat/image.h"

BongoCatResult bongo_cat_image_decode_pixels(const char *path,
    BongoCatImage *image, BongoCatError *error);
BongoCatResult bongo_cat_image_decode_pixels_responsive(const char *path,
    BongoCatImage *image, BongoCatImageProgress progress, void *userdata,
    BongoCatError *error);
void bongo_cat_image_make_alpha_mask_progress(const BongoCatImage *image,
    BongoCatImageAlphaMask *mask, BongoCatImageProgress progress,
    void *userdata);
/* Input pixels must use premultiplied RGBA. */
bool bongo_cat_image_upload_mipmaps(const BongoCatImage *image);

#ifdef _WIN32
bool bongo_cat_image_needs_wic_scaling(const char *path, int limit);
bool bongo_cat_image_decode_wic_responsive(const char *path,
    BongoCatImage *image, int max_width, int max_height,
    BongoCatImageProgress progress, void *userdata);
#endif

#endif
