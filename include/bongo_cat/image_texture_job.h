#ifndef BONGO_CAT_IMAGE_TEXTURE_JOB_H
#define BONGO_CAT_IMAGE_TEXTURE_JOB_H

#include "bongo_cat/image.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BongoCatImageTextureJob BongoCatImageTextureJob;
/* Worker: file I/O, decode and alpha preparation, with one bounded batch.
   Poll/destroy: owning GL thread. Poll uploads at most one batch per call;
   supported drivers are queried without waiting before strip reuse.
   A finished texture is transferred to the caller only on result == 1.
   Result 0 means pending; -1 means failure/cancellation, preserving the old
   texture owned by the caller. Cancellation does not wait for the worker. */
BongoCatImageTextureJob *bongo_cat_image_texture_job_start(const char *path,
    int max_width, int max_height, BongoCatError *error);
int bongo_cat_image_texture_job_poll(BongoCatImageTextureJob *job,
    unsigned int *texture, int *width, int *height,
    BongoCatImageAlphaMask *alpha, BongoCatError *error);
void bongo_cat_image_texture_job_cancel(BongoCatImageTextureJob *job);
/* After a nonzero poll result, release temporary CPU/GL allocations and
   check completion before starting another job. Poll this even while hidden.
   Also covers GL deletions made by the caller before the first cleanup call
   (the replaced atlas). 0 = pending, 1 = retired, -1 = driver failure.
   Do not call the upload poll again once cleanup has started. */
int bongo_cat_image_texture_job_cleanup_poll(BongoCatImageTextureJob *job,
    BongoCatError *error);
void bongo_cat_image_texture_job_destroy(BongoCatImageTextureJob *job);

#ifdef __cplusplus
}
#endif
#endif
