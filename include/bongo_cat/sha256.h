#ifndef BONGO_CAT_SHA256_H
#define BONGO_CAT_SHA256_H

#include "bongo_cat/common.h"

void bongo_cat_sha256_bytes(const void *data, size_t size, char output[65]);
BongoCatResult bongo_cat_sha256_file(const char *path, char output[65], BongoCatError *error);
/* Checks cancellation between 8 KiB reads. Cancellation returns a platform
   error and leaves output empty; an in-progress OS read cannot be interrupted. */
typedef bool (*BongoCatSha256Cancelled)(void *userdata);
BongoCatResult bongo_cat_sha256_file_cancellable(const char *path, char output[65],
    BongoCatSha256Cancelled cancelled, void *userdata, BongoCatError *error);

#endif
