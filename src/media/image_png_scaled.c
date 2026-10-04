#include "image_internal.h"

#include <SDL3/SDL_stdinc.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct PixelSpan {
    int first, last;
    float first_weight, last_weight;
} PixelSpan;

typedef struct PngScale {
    int max_width, max_height;
    int source_width, source_height, next_row, output_row;
    int batch, buffered;
    BongoCatImage image;
    PixelSpan *spans;
    float *horizontal, *accumulated;
    float full_weight;
    BongoCatResult failure;
    BongoCatImageRows consume;
    void *consumer;
    BongoCatImageCancelled cancelled;
    void *cancel_data;
    bool passthrough;
} PngScale;

static bool prepare_scale(PngScale *scale, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    scale->source_width = width;
    scale->source_height = height;
    int target_width = width, target_height = height;
    if (width > scale->max_width || height > scale->max_height) {
        if ((int64_t)scale->max_width * height <=
            (int64_t)scale->max_height * width) {
            target_width = scale->max_width;
            target_height = SDL_max(1, (int)((int64_t)height * target_width / width));
        } else {
            target_height = scale->max_height;
            target_width = SDL_max(1, (int)((int64_t)width * target_height / height));
        }
    }
    if (target_width > INT_MAX / 4 ||
        (size_t)target_width > SIZE_MAX / (4u * sizeof(float)) ||
        (size_t)target_width > SIZE_MAX / sizeof(PixelSpan)) {
        scale->failure = BONGO_CAT_ERROR_MEMORY;
        return false;
    }
    scale->image.width = target_width;
    scale->image.height = target_height;
    if (target_width == width && target_height == height) {
        scale->passthrough = true;
        return true;
    }
    size_t stride = (size_t)target_width * 4u;
    size_t batch = 4u * 1024u * 1024u / stride;
    scale->batch = (int)SDL_min((size_t)target_height,
        SDL_max((size_t)1, SDL_min((size_t)64, batch)));
    scale->image.pixels = malloc(stride * scale->batch);
    scale->spans = malloc((size_t)target_width * sizeof(*scale->spans));
    scale->horizontal = malloc((size_t)target_width * 4u * sizeof(float));
    scale->accumulated = calloc((size_t)target_width * 4u, sizeof(float));
    if (!scale->image.pixels || !scale->spans || !scale->horizontal ||
        !scale->accumulated) {
        scale->failure = BONGO_CAT_ERROR_MEMORY;
        return false;
    }
    scale->full_weight = (float)target_width / width;
    for (int x = 0; x < target_width; ++x) {
        int64_t left = (int64_t)x * width;
        int64_t right = (int64_t)(x + 1) * width;
        PixelSpan *span = &scale->spans[x];
        span->first = (int)(left / target_width);
        span->last = (int)((right - 1) / target_width);
        int64_t first_end = SDL_min(right, (int64_t)(span->first + 1) * target_width);
        span->first_weight = (float)(first_end - left) / width;
        span->last_weight = (float)(right - (int64_t)span->last * target_width) / width;
    }
    return true;
}

static void filter_row(PngScale *scale, const unsigned char *pixels) {
    for (int x = 0; x < scale->image.width; ++x) {
        PixelSpan span = scale->spans[x];
        float *output = scale->horizontal + (size_t)x * 4;
        output[0] = output[1] = output[2] = output[3] = 0.0f;
        for (int source_x = span.first; source_x <= span.last; ++source_x) {
            const unsigned char *pixel = pixels + (size_t)source_x * 4;
            if (!pixel[3]) continue;
            float weight = source_x == span.first ? span.first_weight :
                source_x == span.last ? span.last_weight : scale->full_weight;
            float alpha = pixel[3] * weight;
            output[0] += pixel[0] * alpha;
            output[1] += pixel[1] * alpha;
            output[2] += pixel[2] * alpha;
            output[3] += alpha;
        }
    }
}

static bool finish_row(PngScale *scale) {
    unsigned char *pixels = scale->image.pixels +
        (size_t)scale->buffered * scale->image.width * 4;
    for (int x = 0; x < scale->image.width; ++x) {
        const float *sum = scale->accumulated + (size_t)x * 4;
        unsigned char *pixel = pixels + (size_t)x * 4;
        pixel[3] = (unsigned char)SDL_clamp((int)(sum[3] + .5f), 0, 255);
        for (int c = 0; c < 3; ++c)
            pixel[c] = pixel[3] ? (unsigned char)SDL_clamp(
                (int)(sum[c] / sum[3] + .5f), 0, 255) : 0;
    }
    memset(scale->accumulated, 0, (size_t)scale->image.width * 4u * sizeof(float));
    ++scale->output_row;
    ++scale->buffered;
    if (scale->buffered == scale->batch || scale->output_row == scale->image.height) {
        BongoCatImage rows = {.pixels = scale->image.pixels,
            .width = scale->image.width, .height = scale->buffered};
        if (!scale->consume(scale->consumer, &rows, scale->image.height,
            scale->output_row - scale->buffered)) {
            scale->failure = BONGO_CAT_ERROR_PLATFORM;
            return false;
        }
        scale->buffered = 0;
    }
    return true;
}

static bool consume_rows(void *userdata, BongoCatImage *rows, int height, int y) {
    PngScale *scale = userdata;
    if (scale->cancelled && scale->cancelled(scale->cancel_data)) {
        scale->failure = BONGO_CAT_ERROR_PLATFORM;
        return false;
    }
    if (!y && !prepare_scale(scale, rows->width, height)) return false;
    if ((!scale->image.pixels && !scale->passthrough) || rows->width != scale->source_width ||
        height != scale->source_height || y != scale->next_row ||
        rows->height <= 0 || rows->height > height - y) return false;
    if (scale->passthrough) {
        if (!scale->consume(scale->consumer, rows, height, y)) {
            scale->failure = BONGO_CAT_ERROR_PLATFORM;
            return false;
        }
        scale->next_row += rows->height;
        scale->output_row += rows->height;
        return true;
    }
    for (int row = 0; row < rows->height; ++row) {
        if (scale->cancelled && scale->cancelled(scale->cancel_data)) {
            scale->failure = BONGO_CAT_ERROR_PLATFORM;
            return false;
        }
        filter_row(scale, rows->pixels + (size_t)row * rows->width * 4);
        int64_t begin = (int64_t)(y + row) * scale->image.height;
        int64_t end = (int64_t)(y + row + 1) * scale->image.height;
        while (begin < end) {
            if (scale->output_row >= scale->image.height) return false;
            int64_t bottom = (int64_t)(scale->output_row + 1) * height;
            int64_t stop = SDL_min(end, bottom);
            float weight = (float)(stop - begin) / height;
            for (int x = 0; x < scale->image.width * 4; ++x)
                scale->accumulated[x] += scale->horizontal[x] * weight;
            if (stop == bottom && !finish_row(scale)) return false;
            begin = stop;
        }
    }
    scale->next_row += rows->height;
    return true;
}

BongoCatResult bongo_cat_image_decode_png_scaled_rows(const char *path,
    int max_width, int max_height, BongoCatImageRows consume, void *consumer,
    BongoCatImageProgress progress, void *userdata,
    BongoCatImageCancelled cancelled, void *cancel_data, BongoCatError *error) {
    if (!path || !consume || max_width < 1 || max_height < 1)
        return BONGO_CAT_ERROR_ARGUMENT;
    PngScale scale = {.max_width = max_width, .max_height = max_height,
        .consume = consume, .consumer = consumer,
        .cancelled = cancelled, .cancel_data = cancel_data};
    /* Area filtering uses premultiplied colors, so invisible RGB cannot bleed
       into edges. Emit reduced rows as they complete, retaining only a small
       output strip, two filter rows and the decoder's bounded input strip. */
    bool decoded = bongo_cat_image_decode_png_rows_cancellable(path, consume_rows, &scale,
        progress, userdata, cancelled, cancel_data);
    bool complete = decoded && (scale.image.pixels || scale.passthrough) &&
        scale.next_row == scale.source_height &&
        scale.output_row == scale.image.height && !scale.buffered;
    free(scale.spans);
    free(scale.horizontal);
    free(scale.accumulated);
    bongo_cat_image_free(&scale.image);
    if (cancelled && cancelled(cancel_data)) return BONGO_CAT_ERROR_PLATFORM;
    if (complete) return BONGO_CAT_OK;
    if (scale.failure == BONGO_CAT_ERROR_MEMORY) {
        bongo_cat_error_set(error, scale.failure,
            "Cannot allocate bounded PNG resize buffers: %s", path);
        return scale.failure;
    }
    if (scale.failure != BONGO_CAT_OK) return scale.failure;
    /* Unsupported PNG encodings can still use the general codec fallback. */
    return BONGO_CAT_ERROR_FORMAT;
}
