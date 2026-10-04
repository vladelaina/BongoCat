#include "image_internal.h"
#include "image_texture_cache.h"
#include "image_upload_sync.h"
#include "bongo_cat/gl_api.h"
#include "bongo_cat/model_memory.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

typedef struct ImageProgressStage {
    BongoCatImageProgress progress;
    void *userdata;
    float start, span;
} ImageProgressStage;

static void report_progress(void *userdata, float progress) {
    ImageProgressStage *stage = userdata;
    if (stage && stage->progress)
        stage->progress(stage->userdata, stage->start + stage->span * progress);
}

static bool texture_fits(int width, int height, int limit,
    const char *path, BongoCatError *error) {
    if (width <= limit && height <= limit) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
        "Live2D texture %dx%d exceeds the GPU limit of %d pixels; "
        "cannot preserve the original detail: %s", width, height, limit, path);
    return false;
}

typedef struct ModelRowUpload {
    GLuint texture;
    BongoCatImageUploadBuffer buffer;
    BongoCatImageUploadSync sync;
    int width, height, limit;
    const char *path;
    const char *digest;
    BongoCatImageAlphaMask *alpha;
    BongoCatError *error;
    bool upload_failed;
    bool mipmaps;
    BongoCatImage pending;
    int capacity, uploaded_rows;
    unsigned upload_count;
    uint64_t upload_ns;
    uint64_t allocation_ns, prepare_ns, submit_ns, wait_ns, max_submit_ns;
    ImageProgressStage *stage;
    float last_progress;
    uint64_t callback_ns;
} ModelRowUpload;

typedef bool (*RowDecoder)(const char *, BongoCatImageRows, void *,
    BongoCatImageProgress, void *);

static void stream_progress(void *userdata, float progress) {
    ModelRowUpload *upload = userdata;
    upload->last_progress = progress;
    uint64_t started = SDL_GetTicksNS();
    report_progress(upload->stage, progress);
    upload->callback_ns += SDL_GetTicksNS() - started;
}

static bool wait_rows(ModelRowUpload *upload) {
    uint64_t started = SDL_GetTicksNS(), callbacks = upload->callback_ns;
    bool ok = bongo_cat_image_upload_sync_wait(&upload->sync,
        upload->stage->progress ? stream_progress : NULL, upload,
        upload->last_progress, upload->error);
    uint64_t elapsed = SDL_GetTicksNS() - started -
        (upload->callback_ns - callbacks);
    upload->wait_ns += elapsed;
    upload->upload_ns += elapsed;
    if (!ok) upload->upload_failed = true;
    return ok;
}

static bool flush_rows(ModelRowUpload *upload) {
    if (!upload->pending.height) return true;
    /* Prepare the next CPU batch while the previous GPU copy is in flight.
       Only the one bounded staging surface needs a completion check. */
    uint64_t started = SDL_GetTicksNS();
    bongo_cat_image_premultiply(&upload->pending);
    uint64_t elapsed = SDL_GetTicksNS() - started;
    upload->prepare_ns += elapsed;
    upload->upload_ns += elapsed;
    if (!wait_rows(upload)) return false;
    started = SDL_GetTicksNS();
    bool ok = bongo_cat_image_upload_model_rows_prepared(upload->texture,
        &upload->pending, upload->uploaded_rows, &upload->buffer,
        &upload->sync, upload->error);
    elapsed = SDL_GetTicksNS() - started;
    upload->submit_ns += elapsed;
    upload->upload_ns += elapsed;
    upload->max_submit_ns = SDL_max(upload->max_submit_ns, elapsed);
    ++upload->upload_count;
    upload->uploaded_rows += upload->pending.height;
    upload->pending.height = 0;
    upload->upload_failed = !ok;
    return ok;
}

static bool upload_rows(void *userdata, BongoCatImage *rows, int height, int y) {
    ModelRowUpload *upload = userdata;
    if (!y) {
        upload->width = rows->width;
        upload->height = height;
        uint64_t started = SDL_GetTicksNS();
        if (texture_fits(rows->width, height, upload->limit,
            upload->path, upload->error))
            upload->texture = bongo_cat_image_begin_model_texture(
                rows->width, height, upload->mipmaps, upload->error);
        uint64_t elapsed = SDL_GetTicksNS() - started;
        upload->allocation_ns += elapsed;
        upload->upload_ns += elapsed;
        if (!upload->texture) upload->upload_failed = true;
        if (upload->upload_failed) return false;
        /* Decode in small strips, but amortize GL copies/completion checks
           over a bounded 16 MiB upload. Never queue extra staging surfaces. */
        size_t stride = (size_t)rows->width * 4u;
        size_t capacity = 16u * 1024u * 1024u / stride;
        upload->capacity = (int)SDL_min((size_t)height, SDL_max((size_t)1, capacity));
        upload->pending.width = rows->width;
        if ((size_t)upload->capacity <= SIZE_MAX / stride)
            upload->pending.pixels = malloc(stride * upload->capacity);
        if (!upload->pending.pixels) {
            upload->upload_failed = true;
            bongo_cat_error_set(upload->error, BONGO_CAT_ERROR_MEMORY,
                "Cannot allocate the Live2D texture upload batch");
            return false;
        }
    }
    if (upload->upload_failed) return false;
    bongo_cat_image_alpha_mask_rows(rows, height, y, upload->alpha);
    size_t stride = (size_t)rows->width * 4u;
    for (int row = 0; row < rows->height;) {
        int count = SDL_min(rows->height - row,
            upload->capacity - upload->pending.height);
        memcpy(upload->pending.pixels + (size_t)upload->pending.height * stride,
            rows->pixels + (size_t)row * stride, (size_t)count * stride);
        upload->pending.height += count;
        row += count;
        if (upload->pending.height == upload->capacity && !flush_rows(upload))
            return false;
    }
    return y + rows->height != height || flush_rows(upload);
}

static bool decode_model_rows(const char *path, ModelRowUpload *upload,
    ImageProgressStage *stage, RowDecoder decode, int max_width, int max_height) {
    upload->stage = stage;
    upload->last_progress = 0.0f;
    upload->callback_ns = upload->upload_ns = upload->allocation_ns = 0;
    upload->prepare_ns = upload->submit_ns = upload->wait_ns = 0;
    upload->max_submit_ns = 0;
    upload->upload_count = upload->sync.deferred = upload->sync.blocking = 0;
    uint64_t started = SDL_GetTicksNS();
    bool decoded;
    if (decode) {
        decoded = decode(path, upload_rows, upload,
            stage->progress ? stream_progress : NULL, upload);
    } else {
        BongoCatResult result = bongo_cat_image_decode_cached_scaled_rows(path,
            upload->digest, max_width, max_height, upload_rows, upload,
            stage->progress ? stream_progress : NULL, upload,
            NULL, NULL, upload->error);
        if (result == BONGO_CAT_ERROR_IO && !upload->upload_failed &&
            wait_rows(upload)) {
            /* A cache can become unreadable after validation (for example a
               device I/O failure). Discard its partial GPU upload before a
               single uncached retry; never mix rows or retain both atlases. */
            bongo_cat_image_upload_sync_discard(&upload->sync);
            if (upload->texture) glDeleteTextures(1, &upload->texture);
            bongo_cat_image_release_upload_buffer(&upload->buffer);
            bongo_cat_image_free(&upload->pending);
            upload->texture = 0;
            upload->uploaded_rows = 0;
            if (upload->alpha) memset(upload->alpha, 0, sizeof(*upload->alpha));
            if (upload->error) *upload->error = (BongoCatError){0};
            result = bongo_cat_image_decode_png_scaled_rows(path, max_width,
                max_height, upload_rows, upload,
                stage->progress ? stream_progress : NULL, upload,
                NULL, NULL, upload->error);
        }
        decoded = result == BONGO_CAT_OK;
        /* Retry other codecs for unsupported input, never after an allocation
           or upload failure: that would replace a bounded path with a larger
           allocation under memory pressure. */
        if (!decoded && result != BONGO_CAT_ERROR_FORMAT) {
            upload->upload_failed = true;
            if (upload->error && upload->error->code == BONGO_CAT_OK)
                bongo_cat_error_set(upload->error, result,
                    "Cannot complete the streamed PNG texture upload: %s", path);
        }
    }
    /* A codec fallback can also follow a partial decode. Retire any queued
       copy before its resources are replaced, so recovery stays bounded. */
    if (!upload->upload_failed && !wait_rows(upload)) decoded = false;
    bongo_cat_image_upload_sync_discard(&upload->sync);
    double elapsed_ms = (double)(SDL_GetTicksNS() - started) / 1000000.0;
    bongo_cat_model_memory_log("stream-rows-ready",
        "success=%d total_ms=%.1f upload_ms=%.1f progress_ui_ms=%.1f "
        "uploads=%u batch_rows=%d cpu_upload_batch_mib=%.1f pixels=%dx%d "
        "allocation_ms=%.1f prepare_cpu_ms=%.1f submit_ms=%.1f "
        "wait_ms=%.1f max_submit_ms=%.1f sync_deferred=%u sync_blocking=%u",
        decoded, elapsed_ms, (double)upload->upload_ns / 1000000.0,
        (double)upload->callback_ns / 1000000.0, upload->upload_count, upload->capacity,
        bongo_cat_model_texture_mib(upload->width, upload->capacity, false),
        upload->width, upload->height,
        (double)upload->allocation_ns / 1000000.0,
        (double)upload->prepare_ns / 1000000.0,
        (double)upload->submit_ns / 1000000.0,
        (double)upload->wait_ns / 1000000.0,
        (double)upload->max_submit_ns / 1000000.0,
        upload->sync.deferred, upload->sync.blocking);
    bongo_cat_image_free(&upload->pending);
    bongo_cat_image_release_upload_buffer(&upload->buffer);
    return decoded;
}

static GLuint stream_model(const char *path, int limit, int *width, int *height,
    BongoCatImageAlphaMask *alpha, ImageProgressStage *stage,
    RowDecoder decode, int max_width, int max_height,
    bool *upload_failed, BongoCatError *error, const char *digest) {
    ModelRowUpload upload = {.limit = limit, .path = path,
        .digest = digest, .alpha = alpha, .error = error, .mipmaps = true};
    bool decoded = decode_model_rows(path, &upload, stage, decode, max_width, max_height);
    BongoCatError mip_error = {0};
    if (decoded) bongo_cat_model_memory_log("texture-mipmaps", "pixels=%dx%d",
        upload.width, upload.height);
    if (decoded && !bongo_cat_image_finish_model_texture(upload.texture, &mip_error)) {
        /* Discard partial mip storage, then decode the selected strips again.
           No full-size CPU backup is needed for the linear fallback. */
        glDeleteTextures(1, &upload.texture);
        upload.texture = 0;
        upload.mipmaps = false;
        upload.uploaded_rows = 0;
        upload.upload_count = 0;
        upload.upload_ns = 0;
        bongo_cat_gl_clear_errors();
        stage->start += stage->span;
        stage->span = (1.0f - stage->start) * .25f;
        decoded = decode_model_rows(path, &upload, stage, decode, max_width, max_height);
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
            "Live2D mipmap upload unavailable; retrying %dx%d pixels",
            upload.width, upload.height);
    }
    *upload_failed = upload.upload_failed;
    if (!decoded) {
        if (upload.texture) glDeleteTextures(1, &upload.texture);
        if (alpha) memset(alpha, 0, sizeof(*alpha));
        return 0;
    }
    if (width) *width = upload.width;
    if (height) *height = upload.height;
    SDL_Log("Live2D texture uploaded at %dx%d (streamed): %s",
        upload.width, upload.height, path);
    if (stage->progress) stage->progress(stage->userdata, 1.0f);
    return upload.texture;
}

unsigned int bongo_cat_image_texture_model(const char *path, bool direct_decode,
    int *width, int *height, BongoCatImageAlphaMask *alpha,
    BongoCatImageProgress progress, void *userdata, BongoCatError *error) {
    if (width) *width = 0;
    if (height) *height = 0;
    if (alpha) memset(alpha, 0, sizeof(*alpha));
    if (!path || !path[0]) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "A Live2D texture path is required");
        return 0;
    }
    if (!SDL_GL_GetCurrentContext() || !bongo_cat_gl_clear_errors()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot load a Live2D texture without a usable OpenGL context");
        return 0;
    }
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (glGetError() != GL_NO_ERROR || limit < 1) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot query the GPU texture size limit");
        return 0;
    }
    int source_width = 0, source_height = 0;
    if (bongo_cat_image_info(path, &source_width, &source_height) &&
        !texture_fits(source_width, source_height, limit, path, error)) return 0;

    BongoCatImage image = {0};
    ImageProgressStage stage = {progress, userdata, 0.0f, .30f};
    BongoCatImageProgress staged = progress ? report_progress : NULL;
    // A small window does not imply that the parts of a large atlas are small.
    if (!direct_decode) {
        stage.span = .65f;
        bool upload_failed = false;
        bongo_cat_model_memory_log("png-stream", "decoder=png-rows");
        GLuint texture = stream_model(path, limit, width, height, alpha,
            &stage, bongo_cat_image_decode_png_rows, 0, 0, &upload_failed, error, NULL);
        if (texture || upload_failed) return texture;
#ifdef _WIN32
        stage = (ImageProgressStage){progress, userdata, .80f, .10f};
        bongo_cat_model_memory_log("wic-stream",
            "decoder=wic-rows previous_decoder=png-rows-failed");
        texture = stream_model(path, limit, width, height, alpha,
            &stage, bongo_cat_image_decode_wic_rows, 0, 0, &upload_failed, error, NULL);
        if (texture || upload_failed) return texture;
#endif
        // Unsupported encodings retain the existing general decoder fallback.
        stage = (ImageProgressStage){progress, userdata, .95f, .01f};
    }
    bongo_cat_model_memory_log("stb-full-decode",
        "decoder=stb-full direct=%d", direct_decode);
    if (bongo_cat_image_decode_pixels_responsive(path, &image,
        staged, &stage, error) != BONGO_CAT_OK) return 0;
    bongo_cat_model_memory_log("stb-decoded", "pixels=%dx%d cpu_rgba_mib=%.1f",
        image.width, image.height,
        bongo_cat_model_texture_mib(image.width, image.height, false));
    if (!texture_fits(image.width, image.height, limit, path, error)) {
        bongo_cat_image_free(&image);
        return 0;
    }
    float decoded_progress = stage.start + stage.span;
    if (progress) progress(userdata, decoded_progress);
    stage = (ImageProgressStage){progress, userdata, decoded_progress,
        (1.0f - decoded_progress) * .5f};
    bongo_cat_image_make_alpha_mask_progress(&image, alpha, staged, &stage);
    bongo_cat_model_memory_log("texture-upload", "pixels=%dx%d",
        image.width, image.height);
    GLuint texture = bongo_cat_image_upload_texture(&image, 0, true, error);
    if (texture) {
        if (width) *width = image.width;
        if (height) *height = image.height;
        SDL_Log("Live2D texture preserved at %dx%d: %s", image.width, image.height, path);
    } else {
        if (alpha) memset(alpha, 0, sizeof(*alpha));
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Live2D texture failed: %s (%s)",
            path, error ? error->message : "upload failed");
    }
    bongo_cat_image_free(&image);
    if (progress) progress(userdata, 1.0f);
    return texture;
}

typedef struct ScaledModelProgress {
    BongoCatImageProgress progress;
    void *userdata;
    float start, span;
} ScaledModelProgress;

static void scaled_model_progress(void *userdata, float progress) {
    ScaledModelProgress *stage = userdata;
    if (stage && stage->progress)
        stage->progress(stage->userdata,
            stage->start + stage->span * SDL_clamp(progress, 0.0f, 1.0f));
}

unsigned int bongo_cat_image_texture_model_scaled(const char *path,
    bool direct_decode, int max_width, int max_height, int *width, int *height,
    BongoCatImageAlphaMask *alpha, BongoCatImageProgress progress,
    void *userdata, BongoCatError *error) {
    return bongo_cat_image_texture_model_scaled_cached(path, NULL, direct_decode,
        max_width, max_height, width, height, alpha, progress, userdata, error);
}

unsigned int bongo_cat_image_texture_model_scaled_cached(const char *path,
    const char *digest, bool direct_decode, int max_width, int max_height,
    int *width, int *height, BongoCatImageAlphaMask *alpha,
    BongoCatImageProgress progress, void *userdata, BongoCatError *error) {
    if (width) *width = 0;
    if (height) *height = 0;
    if (alpha) memset(alpha, 0, sizeof(*alpha));
    if (!path || max_width < 1 || max_height < 1) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "A valid Live2D texture resize bound is required");
        return 0;
    }
    int source_width = 0, source_height = 0;
    bool source_size_known = bongo_cat_image_info(path, &source_width,
        &source_height);
    if (source_size_known && source_width <= max_width &&
        source_height <= max_height)
        return bongo_cat_image_texture_model(path, direct_decode, width, height,
            alpha, progress, userdata, error);

    if (!SDL_GL_GetCurrentContext() || !bongo_cat_gl_clear_errors()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot load a Live2D texture without a usable OpenGL context");
        return 0;
    }
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (glGetError() != GL_NO_ERROR || limit < 1) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot query the GPU texture size limit");
        return 0;
    }
    max_width = SDL_min(max_width, limit);
    max_height = SDL_min(max_height, limit);
    /* Filter, alpha-mask and upload incrementally, using the same target size
       and pixel filter as before. The reduced atlas never exists as a full
       CPU image, and immutable mip storage avoids growing the GPU allocation. */
    ImageProgressStage png_stage = {progress, userdata, 0.0f, .85f};
    bool upload_failed = false;
    bongo_cat_model_memory_log("png-scaled-stream",
        "bound=%dx%d cpu_full_image=0", max_width, max_height);
    GLuint streamed = stream_model(path, limit, width, height, alpha,
        &png_stage, NULL, max_width, max_height, &upload_failed, error, digest);
    if (streamed || upload_failed) return streamed;

    /* General codecs retain the fallback for other image encodings. */
    BongoCatImage image = {0};
    BongoCatImage source = {0};
    bool decoded = false;
#ifdef _WIN32
    ScaledModelProgress decode_stage = {progress, userdata, 0.0f, .70f};
    if (!decoded) {
        bongo_cat_model_memory_log("wic-scaled-decode", "bound=%dx%d",
            max_width, max_height);
        decoded = bongo_cat_image_decode_wic_responsive(path, &image,
            max_width, max_height, progress ? scaled_model_progress : NULL,
            &decode_stage);
        bongo_cat_model_memory_log("wic-scaled-decoded",
            "success=%d pixels=%dx%d cpu_rgba_mib=%.1f",
            decoded, image.width, image.height,
            bongo_cat_model_texture_mib(image.width, image.height, false));
    }
#endif
    if (!decoded) {
        ImageProgressStage stage = {progress, userdata, 0.0f, .45f};
        bongo_cat_model_memory_log("stb-full-fallback",
            "decoder=stb-full bound=%dx%d", max_width, max_height);
        if (bongo_cat_image_decode_pixels_responsive(path, &source,
            progress ? report_progress : NULL, &stage, error) != BONGO_CAT_OK) {
            bongo_cat_image_free(&source);
            return 0;
        }
        bongo_cat_model_memory_log("stb-decoded",
            "pixels=%dx%d cpu_rgba_mib=%.1f",
            source.width, source.height,
            bongo_cat_model_texture_mib(source.width, source.height, false));
        decoded = bongo_cat_image_resize_rgba_take(&source, max_width, max_height,
            &image, error);
        bongo_cat_model_memory_log("stb-resized", "success=%d pixels=%dx%d",
            decoded, image.width, image.height);
    }
    bongo_cat_image_free(&source);
    if (!decoded || !image.pixels) {
        bongo_cat_image_free(&image);
        if (alpha) memset(alpha, 0, sizeof(*alpha));
        return 0;
    }
    /* Only the reduced atlas reaches GL; the original source is already gone. */
    if (progress) progress(userdata, .70f);
    ScaledModelProgress alpha_stage = {progress, userdata, .70f, .15f};
    bongo_cat_image_make_alpha_mask_progress(&image, alpha,
        progress ? scaled_model_progress : NULL, &alpha_stage);
    if (progress) progress(userdata, .85f);
    bongo_cat_model_memory_log("texture-upload", "pixels=%dx%d",
        image.width, image.height);
    GLuint texture = bongo_cat_image_upload_texture(&image, 0, true, error);
    if (texture) {
        if (width) *width = image.width;
        if (height) *height = image.height;
        if (source_size_known)
            SDL_Log("Live2D texture downsampled from %dx%d to %dx%d: %s",
                source_width, source_height, image.width, image.height, path);
        else
            SDL_Log("Live2D texture loaded with bounded decode %dx%d: %s",
                image.width, image.height, path);
    } else {
        if (alpha) memset(alpha, 0, sizeof(*alpha));
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
            "Live2D downsampled texture failed: %s (%s)", path,
            error ? error->message : "upload failed");
    }
    bongo_cat_image_free(&image);
    if (progress) progress(userdata, 1.0f);
    return texture;
}
