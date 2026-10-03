#include "bongo_cat/image_texture_job.h"
#include "image_texture_cache.h"
#include "image_upload_sync.h"
#include "bongo_cat/gl_api.h"
#include "bongo_cat/sha256.h"
#include "bongo_cat/log.h"

#include <SDL3/SDL.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct BongoCatImageTextureJob {
    char path[BONGO_CAT_PATH_CAP];
    int max_width, max_height;
    SDL_Thread *thread;
    SDL_Mutex *mutex;
    SDL_Condition *condition;
    SDL_AtomicInt cancelled;
    /* Protected by mutex. Worker never holds it during I/O or decoding. */
    BongoCatImage pending;
    int capacity, height, produced;
    bool ready, done;
    BongoCatResult result;
    BongoCatError failure;
    /* Worker-only until done is published under mutex. */
    int prepared;
    BongoCatImageAlphaMask alpha;
    uint64_t prepare_ns;
    /* GL-thread-only state. */
    unsigned int texture;
    int uploaded;
    BongoCatImageUploadBuffer transfer;
    BongoCatImageUploadSync sync;
    BongoCatError upload_failure;
    BongoCatError cleanup_failure;
    uint64_t upload_ns, max_batch_ns, mip_ns;
    bool mip_submitted, cleanup_started;
    unsigned uploads;
};

static bool cancelled(void *userdata) {
    BongoCatImageTextureJob *job = userdata;
    return SDL_GetAtomicInt(&job->cancelled) != 0;
}

static bool publish_batch(BongoCatImageTextureJob *job) {
    if (cancelled(job)) return false;
    uint64_t started = SDL_GetTicksNS();
    bongo_cat_image_alpha_mask_rows(&job->pending, job->height,
        job->prepared, &job->alpha);
    bongo_cat_image_premultiply(&job->pending);
    job->prepared += job->pending.height;
    job->prepare_ns += SDL_GetTicksNS() - started;
    /* Only the private transfer copy is premultiplied. Decoder/cache rows
       remain straight RGBA, so cached and uncached results stay identical. */
    job->ready = true;
    while (job->ready && !cancelled(job))
        SDL_WaitCondition(job->condition, job->mutex);
    return !cancelled(job);
}

static bool queue_rows(void *userdata, BongoCatImage *rows, int height, int y) {
    BongoCatImageTextureJob *job = userdata;
    SDL_LockMutex(job->mutex);
    bool ok = false;
    if (cancelled(job)) goto done;
    if (!y) {
        if (rows->width < 1 || rows->width > job->max_width ||
            height < 1 || height > job->max_height || rows->width > INT_MAX / 4)
            goto done;
        size_t stride = (size_t)rows->width * 4;
        job->capacity = (int)SDL_min((size_t)height,
            SDL_max((size_t)1, 2u * 1024u * 1024u / stride));
        job->pending.pixels = malloc(stride * (size_t)job->capacity);
        if (!job->pending.pixels) {
            bongo_cat_error_set(&job->failure, BONGO_CAT_ERROR_MEMORY,
                "Cannot allocate the texture refresh transfer batch");
            goto done;
        }
        job->pending.width = rows->width;
        job->height = height;
    }
    if (y != job->produced || height != job->height ||
        rows->width != job->pending.width || rows->height <= 0 ||
        rows->height > height - y) goto done;
    size_t stride = (size_t)rows->width * 4;
    for (int row = 0; row < rows->height;) {
        int count = SDL_min(rows->height - row, job->capacity - job->pending.height);
        memcpy(job->pending.pixels + (size_t)job->pending.height * stride,
            rows->pixels + (size_t)row * stride, (size_t)count * stride);
        row += count;
        job->pending.height += count;
        if (job->pending.height == job->capacity && !publish_batch(job)) goto done;
    }
    job->produced += rows->height;
    if (job->produced == height && job->pending.height && !publish_batch(job)) goto done;
    ok = true;
done:
    SDL_UnlockMutex(job->mutex);
    return ok;
}

static BongoCatResult decode_fallback(BongoCatImageTextureJob *job) {
    BongoCatImage image = {0}, source = {0};
    bool decoded = false;
#ifdef _WIN32
    decoded = bongo_cat_image_decode_wic_cancellable(job->path, &image,
        job->max_width, job->max_height, cancelled, job);
#endif
    if (!decoded && !cancelled(job) &&
        bongo_cat_image_decode_pixels(job->path, &source, &job->failure) == BONGO_CAT_OK &&
        !cancelled(job))
        decoded = bongo_cat_image_resize_rgba_take(&source, job->max_width,
            job->max_height, &image, &job->failure);
    bongo_cat_image_free(&source);
    bool ok = decoded && !cancelled(job);
    for (int y = 0; ok && y < image.height; y += 64) {
        BongoCatImage rows = {.pixels = image.pixels + (size_t)y * image.width * 4,
            .width = image.width, .height = SDL_min(64, image.height - y)};
        ok = queue_rows(job, &rows, image.height, y);
    }
    bongo_cat_image_free(&image);
    return ok ? BONGO_CAT_OK : BONGO_CAT_ERROR_FORMAT;
}

static int SDLCALL decode_worker(void *userdata) {
    BongoCatImageTextureJob *job = userdata;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    uint64_t before_size = 0, before_time = 0, after_size = 0, after_time = 0;
    bool source_known = bongo_cat_path_file_info(job->path, &before_size, &before_time);
    char digest[65] = {0};
    bool hashed = !cancelled(job) &&
        bongo_cat_sha256_file_cancellable(job->path, digest, cancelled, job, NULL) == BONGO_CAT_OK;
    BongoCatResult result = BONGO_CAT_ERROR_IO;
    bool stable = source_known && bongo_cat_path_file_info(job->path,
        &after_size, &after_time) && before_size == after_size && before_time == after_time;
    if (!cancelled(job) && (!source_known || stable))
        result = bongo_cat_image_decode_cached_scaled_rows(job->path,
            hashed && stable ? digest : NULL, job->max_width, job->max_height,
            queue_rows, job, NULL, NULL, cancelled, job, &job->failure);
    /* Unsupported input may use the same codecs as initial loading. Never
       restart after publishing partial rows or after an allocation failure. */
    if (result == BONGO_CAT_ERROR_FORMAT && !job->produced &&
        !job->pending.pixels && !cancelled(job)) result = decode_fallback(job);
    if (result == BONGO_CAT_OK && source_known &&
        (!bongo_cat_path_file_info(job->path, &after_size, &after_time) ||
         before_size != after_size || before_time != after_time)) {
        result = BONGO_CAT_ERROR_IO;
        if (hashed) bongo_cat_image_forget_texture_cache(digest,
            job->max_width, job->max_height);
        bongo_cat_error_set(&job->failure, result, "Texture source changed during refresh");
    }
    SDL_LockMutex(job->mutex);
    job->result = result;
    job->done = true;
    SDL_UnlockMutex(job->mutex);
    return 0;
}

BongoCatImageTextureJob *bongo_cat_image_texture_job_start(const char *path,
    int max_width, int max_height, BongoCatError *error) {
    if (!path || strlen(path) >= BONGO_CAT_PATH_CAP || max_width < 1 || max_height < 1) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
            "Invalid background texture refresh request");
        return NULL;
    }
    BongoCatImageTextureJob *job = calloc(1, sizeof(*job));
    if (!job) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate the texture refresh job");
        return NULL;
    }
    memcpy(job->path, path, strlen(path) + 1);
    job->max_width = max_width; job->max_height = max_height;
    job->mutex = SDL_CreateMutex();
    job->condition = SDL_CreateCondition();
    if (job->mutex && job->condition)
        job->thread = SDL_CreateThread(decode_worker, BONGO_CAT_SLUG "-texture-resize", job);
    if (!job->thread) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot start texture refresh worker: %s", SDL_GetError());
        bongo_cat_image_texture_job_destroy(job);
        return NULL;
    }
    return job;
}

void bongo_cat_image_texture_job_cancel(BongoCatImageTextureJob *job) {
    if (!job) return;
    SDL_SetAtomicInt(&job->cancelled, 1);
    if (job->mutex && job->condition) {
        SDL_LockMutex(job->mutex);
        SDL_SignalCondition(job->condition);
        SDL_UnlockMutex(job->mutex);
    }
}

static void release_job_gpu(BongoCatImageTextureJob *job) {
    /* GL-thread-only objects; the decoder never touches them. They may be
       released after the copy completes even if the worker is still stopping. */
    if (job->texture) {
        glDeleteTextures(1, &job->texture);
        job->texture = 0;
    }
    bongo_cat_image_release_upload_buffer(&job->transfer);
}

int bongo_cat_image_texture_job_poll(BongoCatImageTextureJob *job,
    unsigned int *texture, int *width, int *height,
    BongoCatImageAlphaMask *alpha, BongoCatError *error) {
    if (!job) return 0;
    /* Cancellation stops new uploads, but must still retire the last copy.
       Otherwise rapid resizing can allocate the next atlas while the cancelled
       job's destination/staging storage is still in use by the GPU. */
    int ready = bongo_cat_image_upload_sync_poll(&job->sync, error);
    if (!ready) return 0;
    if (ready < 0) {
        if (error) job->upload_failure = *error;
        bongo_cat_image_texture_job_cancel(job);
    }
    bool ok = !cancelled(job);
    if (!ok && (job->texture || job->transfer.texture || job->transfer.framebuffer)) {
        release_job_gpu(job);
        /* Submit deletes even if the hidden window will not swap buffers.
           Final cleanup still fences them before starting another job. */
        glFlush();
    }
    if (!SDL_TryLockMutex(job->mutex)) return 0;
    ok = ok && !cancelled(job);
    if (ok && job->ready) {
        uint64_t started = SDL_GetTicksNS();
        if (!job->texture)
            job->texture = bongo_cat_image_begin_model_texture(
                job->pending.width, job->height, true, error);
        ok = job->texture != 0;
        if (ok)
            ok = bongo_cat_image_upload_model_rows_prepared(job->texture,
                &job->pending, job->uploaded, &job->transfer, &job->sync, error);
        if (ok) job->uploaded += job->pending.height;
        else {
            SDL_SetAtomicInt(&job->cancelled, 1);
            /* Preserve upload errors until the worker acknowledges cancel. */
            if (error) job->upload_failure = *error;
        }
        uint64_t elapsed = SDL_GetTicksNS() - started;
        job->upload_ns += elapsed;
        job->max_batch_ns = SDL_max(job->max_batch_ns, elapsed);
        ++job->uploads;
        job->pending.height = 0;
        job->ready = false;
        SDL_SignalCondition(job->condition);
    }
    bool done = job->done;
    BongoCatResult result = job->result;
    if (done && error) {
        if (job->upload_failure.code != BONGO_CAT_OK) *error = job->upload_failure;
        else if (job->failure.code != BONGO_CAT_OK) *error = job->failure;
    }
    SDL_UnlockMutex(job->mutex);
    if (!done) return 0;
    if (!ok || result != BONGO_CAT_OK || job->uploaded != job->height || !job->texture)
        return -1;
    if (!job->mip_submitted) {
        uint64_t started = SDL_GetTicksNS();
        if (!bongo_cat_image_finish_model_texture(job->texture, error)) return -1;
        GLenum status = bongo_cat_image_upload_sync_submit(&job->sync);
        if (status != GL_NO_ERROR) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
                "Cannot track texture mipmap completion (0x%x)", (unsigned)status);
            return -1;
        }
        job->mip_ns = SDL_GetTicksNS() - started;
        job->mip_submitted = true;
        /* Keep drawing the old atlas until the complete mip chain is ready. */
        return 0;
    }
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[texture-refresh] result=uploaded batches=%u upload_ms=%.1f "
        "max_batch_ms=%.1f mip_submit_ms=%.1f prepare_worker_ms=%.1f "
        "sync_deferred=%u sync_blocking=%u cpu_batch_mib=%.1f",
        job->uploads, (double)job->upload_ns / 1000000.0,
        (double)job->max_batch_ns / 1000000.0,
        (double)job->mip_ns / 1000000.0,
        (double)job->prepare_ns / 1000000.0,
        job->sync.deferred, job->sync.blocking,
        (double)job->pending.width * job->capacity * 4 / (1024.0 * 1024.0));
    *texture = job->texture; job->texture = 0;
    *width = job->pending.width; *height = job->height;
    if (alpha) *alpha = job->alpha;
    return 1;
}

static void release_job_storage(BongoCatImageTextureJob *job) {
    release_job_gpu(job);
    bongo_cat_image_free(&job->pending);
}

static int poll_cleanup_completion(BongoCatImageTextureJob *job,
    BongoCatError *error) {
    int result = bongo_cat_image_upload_sync_poll(&job->sync, error);
    if (result > 0 && job->cleanup_failure.code != BONGO_CAT_OK) {
        if (error) *error = job->cleanup_failure;
        return -1;
    }
    return result;
}

int bongo_cat_image_texture_job_cleanup_poll(BongoCatImageTextureJob *job,
    BongoCatError *error) {
    if (!job) return 1;
    if (job->cleanup_started)
        return poll_cleanup_completion(job, error);
    /* The terminal upload poll has observed worker completion. Joining here
       cannot wait for another batch. Also poll any fence left behind by a
       failed submission before freeing the staging storage. */
    const int ready = bongo_cat_image_upload_sync_poll(&job->sync, &job->cleanup_failure);
    if (!ready) return 0;
    if (job->thread) {
        SDL_WaitThread(job->thread, NULL);
        job->thread = NULL;
    }
    release_job_storage(job);
    job->cleanup_started = true;
    GLenum status = bongo_cat_image_upload_sync_submit(&job->sync);
    if (status != GL_NO_ERROR) {
        /* A failed fence must not let successive allocations outrun GPU work.
           Recover only on this error path; normal cleanup is polled. */
        glFinish();
        bongo_cat_image_upload_sync_discard(&job->sync);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Texture cleanup completion failed (GL 0x%x)", (unsigned)status);
        return -1;
    }
    /* Completion fences order GPU work; drivers may still cache freed
       storage. They do not guarantee an immediate drop in process residency. */
    return poll_cleanup_completion(job, error);
}

void bongo_cat_image_texture_job_destroy(BongoCatImageTextureJob *job) {
    if (!job) return;
    bongo_cat_image_texture_job_cancel(job);
    /* Model teardown may bypass normal polling. Normal resize cleanup has
       already completed and incurs no blocking wait here. */
    bongo_cat_image_upload_sync_retire(&job->sync);
    release_job_gpu(job);
    if (job->thread) SDL_WaitThread(job->thread, NULL);
    release_job_storage(job);
    if (job->condition) SDL_DestroyCondition(job->condition);
    if (job->mutex) SDL_DestroyMutex(job->mutex);
    free(job);
}
