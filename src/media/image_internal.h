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
/* CPU-only conversion, shared by synchronous and background uploads. */
void bongo_cat_image_premultiply(BongoCatImage *image);
/* Input pixels must use premultiplied RGBA. */
bool bongo_cat_image_upload_mipmaps(const BongoCatImage *image);
bool bongo_cat_image_generate_mipmaps(void);
/* Model uploads premultiply the decoded pixels in place, exactly once.
   Existing textures are only supported for straight-alpha image updates. */
unsigned int bongo_cat_image_upload_texture(BongoCatImage *image,
    unsigned int existing, bool model, BongoCatError *error);
unsigned int bongo_cat_image_begin_model_texture(int width, int height,
    bool mipmaps, BongoCatError *error);
typedef struct BongoCatImageUploadBuffer {
    unsigned int texture, framebuffer;
    int width, height;
} BongoCatImageUploadBuffer;
void bongo_cat_image_release_upload_buffer(BongoCatImageUploadBuffer *buffer);
bool bongo_cat_image_upload_model_rows(unsigned int texture,
    BongoCatImage *rows, int y, BongoCatImageUploadBuffer *buffer,
    BongoCatError *error);
struct BongoCatImageUploadSync;
/* Rows already contain premultiplied RGBA. Caller must poll sync before
   submitting another strip or generating mipmaps. */
bool bongo_cat_image_upload_model_rows_prepared(unsigned int texture,
    const BongoCatImage *rows, int y, BongoCatImageUploadBuffer *buffer,
    struct BongoCatImageUploadSync *sync, BongoCatError *error);
bool bongo_cat_image_finish_model_texture(unsigned int texture,
    BongoCatError *error);
void bongo_cat_image_alpha_mask_rows(const BongoCatImage *rows, int height,
    int y, BongoCatImageAlphaMask *mask);

/* Row callbacks run on the calling thread; decoding uses one bounded buffer.
   Returning false cancels decoding before producing another strip. */
typedef bool (*BongoCatImageRows)(void *userdata, BongoCatImage *rows,
    int height, int y);
typedef bool (*BongoCatImageCancelled)(void *userdata);
/* Fast path for PNG8 RGB/RGBA without interlacing or tRNS. Other encodings
   return false so the caller can use its general decoder. */
bool bongo_cat_image_decode_png_rows(const char *path,
    BongoCatImageRows consume, void *consumer,
    BongoCatImageProgress progress, void *userdata);
/* Same input formats and callback lifetime as png_rows. Outputs straight RGBA
   in reduced strips; no complete source or reduced CPU image is retained. */
BongoCatResult bongo_cat_image_decode_png_scaled_rows(const char *path,
    int max_width, int max_height, BongoCatImageRows consume, void *consumer,
    BongoCatImageProgress progress, void *userdata,
    BongoCatImageCancelled cancelled, void *cancel_data, BongoCatError *error);
/* Consumes source on success, moving its allocation when no resize is needed.
   On failure source remains owned by the caller. Target must be empty. */
bool bongo_cat_image_resize_rgba_take(BongoCatImage *source,
    int max_width, int max_height, BongoCatImage *target,
    BongoCatError *error);
#ifdef _WIN32
bool bongo_cat_image_decode_wic_rows(const char *path,
    BongoCatImageRows consume, void *consumer,
    BongoCatImageProgress progress, void *userdata);
bool bongo_cat_image_decode_wic_responsive(const char *path,
    BongoCatImage *image, int max_width, int max_height,
    BongoCatImageProgress progress, void *userdata);
/* Runs on the calling worker. Checks cancellation before allocating output
   and between WIC CopyPixels strips; a codec call itself cannot be interrupted. */
bool bongo_cat_image_decode_wic_cancellable(const char *path,
    BongoCatImage *image, int max_width, int max_height,
    BongoCatImageCancelled cancelled, void *cancel_data);
#endif

#endif
