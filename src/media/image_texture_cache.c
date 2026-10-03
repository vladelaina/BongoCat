#include "image_texture_cache.h"
#include "bongo_cat/file.h"
#include "bongo_cat/log.h"

#include <SDL3/SDL.h>
#include <miniz.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#define cache_process_id _getpid
#else
#include <unistd.h>
#define cache_process_id getpid
#endif

enum { HEADER_SIZE = 96, IO_BYTES = 1024 * 1024 };
static const unsigned char signature[8] = {'B', 'C', 'R', 'G', 'B', 'A', '0', '1'};

static void put32(unsigned char *p, uint32_t value) {
    for (int i = 0; i < 4; ++i) p[i] = (unsigned char)(value >> (8 * i));
}
static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static bool stopped(BongoCatImageCancelled cancel, void *data) {
    return cancel && cancel(data);
}

/* Validate the entire cached atlas before exposing any rows to GL.
   A corrupt cache can then fall back without leaking a partially uploaded
   texture or mixing rows from two sources. Large atlases use two bounded
   passes; small atlases reuse the bytes already retained for validation. */
static BongoCatResult read_cache(FILE *file, const char *digest, int max_width,
    int max_height, BongoCatImageRows consume, void *consumer,
    BongoCatImageProgress progress, void *userdata,
    BongoCatImageCancelled cancel, void *cancel_data) {
    unsigned char header[HEADER_SIZE];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, signature, sizeof(signature)) ||
        memcmp(header + 32, digest, 64) ||
        get32(header + 8) != (uint32_t)max_width ||
        get32(header + 12) != (uint32_t)max_height) return BONGO_CAT_ERROR_FORMAT;
    uint32_t check = get32(header + 28);
    put32(header + 28, 0);
    if ((uint32_t)mz_crc32(0, header, sizeof(header)) != check)
        return BONGO_CAT_ERROR_FORMAT;
    uint32_t width = get32(header + 16), height = get32(header + 20);
    uint64_t bytes = (uint64_t)width * height * 4;
    if (!width || !height || width > (uint32_t)max_width ||
        height > (uint32_t)max_height || width > INT_MAX / 4 ||
        bytes > BONGO_CAT_TEXTURE_CACHE_ENTRY_LIMIT - HEADER_SIZE)
        return BONGO_CAT_ERROR_FORMAT;
    size_t stride = (size_t)width * 4;
    int batch = (int)SDL_min((size_t)height,
        SDL_max((size_t)1, (size_t)IO_BYTES / stride));
    size_t capacity = stride * (size_t)batch;
    unsigned char *buffer = malloc(capacity);
    if (!buffer) return BONGO_CAT_ERROR_FORMAT; /* Source decode can still work. */
    uint64_t remaining = bytes;
    mz_ulong crc = 0;
    BongoCatResult result = BONGO_CAT_ERROR_FORMAT;
    while (remaining) {
        if (stopped(cancel, cancel_data)) { result = BONGO_CAT_ERROR_PLATFORM; goto done; }
        size_t count = (size_t)SDL_min(remaining, (uint64_t)capacity);
        if (fread(buffer, 1, count, file) != count) goto done;
        crc = mz_crc32(crc, buffer, count);
        remaining -= count;
        if (progress) progress(userdata, .1f * (float)(bytes - remaining) / (float)bytes);
    }
    if ((uint32_t)crc != get32(header + 24) || fgetc(file) != EOF ||
        ferror(file)) goto done;
    if (bytes == (uint64_t)capacity) {
        /* The complete small atlas already fits in the validation strip.
           Expose it only after checking CRC and EOF, without rereading or
           reserving a full 1 MiB for a tiny quality setting. */
        if (stopped(cancel, cancel_data)) { result = BONGO_CAT_ERROR_PLATFORM; goto done; }
        BongoCatImage rows = {.pixels = buffer, .width = (int)width, .height = (int)height};
        result = consume(consumer, &rows, (int)height, 0) ?
            BONGO_CAT_OK : BONGO_CAT_ERROR_PLATFORM;
        if (result == BONGO_CAT_OK && progress) progress(userdata, 1.0f);
        goto done;
    }
    if (fseek(file, HEADER_SIZE, SEEK_SET) != 0) goto done;
    result = BONGO_CAT_ERROR_IO;
    for (int y = 0; y < (int)height;) {
        if (stopped(cancel, cancel_data)) { result = BONGO_CAT_ERROR_PLATFORM; goto done; }
        int count = SDL_min(batch, (int)height - y);
        size_t size = stride * (size_t)count;
        if (fread(buffer, 1, size, file) != size) goto done;
        BongoCatImage rows = {.pixels = buffer, .width = (int)width, .height = count};
        if (!consume(consumer, &rows, (int)height, y)) {
            result = BONGO_CAT_ERROR_PLATFORM; goto done;
        }
        y += count;
        if (progress) progress(userdata, .1f + .9f * (float)y / height);
    }
    result = BONGO_CAT_OK;
done:
    free(buffer);
    return result;
}

typedef struct CacheWriter {
    FILE *file;
    char target[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    const char *digest;
    const char *skip_reason;
    int max_width, max_height, width, height, next_row;
    mz_ulong crc;
    BongoCatImageRows consume;
    void *consumer;
    BongoCatImageCancelled cancel;
    void *cancel_data;
} CacheWriter;

static void discard(CacheWriter *writer) {
    if (writer->file) fclose(writer->file);
    writer->file = NULL;
    if (writer->temporary[0]) bongo_cat_file_remove(writer->temporary);
    writer->temporary[0] = '\0';
}

static void begin_write(CacheWriter *writer, int width, int height) {
    uint64_t bytes = (uint64_t)width * height * 4 + HEADER_SIZE;
    if (!writer->target[0]) return;
    if (bytes > BONGO_CAT_TEXTURE_CACHE_ENTRY_LIMIT) {
        writer->skip_reason = "entry-too-large";
        return;
    }
    if (!bongo_cat_texture_cache_reserve(bytes)) {
        writer->skip_reason = "storage-unavailable";
        return;
    }
    writer->skip_reason = "write-failed";
    int length = snprintf(writer->temporary, sizeof(writer->temporary),
        "%s.%lu.%llu.%llu.tmp", writer->target, (unsigned long)cache_process_id(),
        (unsigned long long)SDL_GetCurrentThreadID(), (unsigned long long)SDL_GetTicksNS());
    if (length < 0 || (size_t)length >= sizeof(writer->temporary)) {
        writer->temporary[0] = '\0'; return;
    }
    writer->file = bongo_cat_file_open(writer->temporary, "wbx");
    if (!writer->file) { writer->temporary[0] = '\0'; return; }
    unsigned char header[HEADER_SIZE] = {0};
    if (fwrite(header, 1, sizeof(header), writer->file) != sizeof(header)) discard(writer);
}

static bool cache_rows(void *userdata, BongoCatImage *rows, int height, int y) {
    CacheWriter *writer = userdata;
    if (stopped(writer->cancel, writer->cancel_data)) return false;
    if (!y) {
        writer->width = rows->width; writer->height = height;
        begin_write(writer, rows->width, height);
    }
    if (y != writer->next_row || rows->width != writer->width ||
        height != writer->height || rows->height <= 0 || rows->height > height - y)
        return false;
    if (writer->file) {
        size_t bytes = (size_t)rows->width * rows->height * 4;
        writer->crc = mz_crc32(writer->crc, rows->pixels, bytes);
        if (fwrite(rows->pixels, 1, bytes, writer->file) != bytes) discard(writer);
    }
    writer->next_row += rows->height;
    return writer->consume(writer->consumer, rows, height, y);
}

static bool finish_write(CacheWriter *writer) {
    if (!writer->file || writer->next_row != writer->height) return false;
    unsigned char header[HEADER_SIZE] = {0};
    memcpy(header, signature, sizeof(signature));
    put32(header + 8, (uint32_t)writer->max_width);
    put32(header + 12, (uint32_t)writer->max_height);
    put32(header + 16, (uint32_t)writer->width);
    put32(header + 20, (uint32_t)writer->height);
    put32(header + 24, (uint32_t)writer->crc);
    memcpy(header + 32, writer->digest, 64);
    put32(header + 28, (uint32_t)mz_crc32(0, header, sizeof(header)));
    bool ok = fseek(writer->file, 0, SEEK_SET) == 0 &&
        fwrite(header, 1, sizeof(header), writer->file) == sizeof(header);
    if (fclose(writer->file) != 0) ok = false;
    writer->file = NULL;
    if (ok) ok = bongo_cat_file_replace(writer->temporary, writer->target, false);
    if (!ok) bongo_cat_file_remove(writer->temporary);
    writer->temporary[0] = '\0';
    /* Other pet processes can finish writers concurrently. Reconcile the
       soft disk budget after publication, as well as before allocation. */
    if (ok) bongo_cat_texture_cache_reserve(0);
    return ok;
}

BongoCatResult bongo_cat_image_decode_cached_scaled_rows(const char *source,
    const char *digest, int max_width, int max_height,
    BongoCatImageRows consume, void *consumer, BongoCatImageProgress progress,
    void *userdata, BongoCatImageCancelled cancelled, void *cancel_data,
    BongoCatError *error) {
    if (!source || !consume || max_width < 1 || max_height < 1)
        return BONGO_CAT_ERROR_ARGUMENT;
    CacheWriter writer = {.digest = digest, .max_width = max_width,
        .max_height = max_height, .consume = consume, .consumer = consumer,
        .cancel = cancelled, .cancel_data = cancel_data,
        .skip_reason = "source-unavailable"};
    bool configured = bongo_cat_texture_cache_path(digest, max_width, max_height,
        writer.target, sizeof(writer.target));
    uint64_t started = SDL_GetTicksNS(), before_size = 0, before_time = 0;
    bool stable = configured && bongo_cat_path_file_info(source, &before_size, &before_time);
    FILE *file = stable ? bongo_cat_file_open(writer.target, "rb") : NULL;
    if (file) {
        BongoCatResult cached = read_cache(file, digest, max_width, max_height,
            consume, consumer, progress, userdata, cancelled, cancel_data);
        fclose(file);
        if (cached == BONGO_CAT_OK) {
            SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
                "[texture-cache] result=hit bound=%dx%d elapsed_ms=%.1f",
                max_width, max_height, (double)(SDL_GetTicksNS() - started) / 1000000.0);
            return cached;
        }
        if (cached != BONGO_CAT_ERROR_FORMAT) {
            if (!error || error->code == BONGO_CAT_OK)
                bongo_cat_error_set(error, cached, "Cached texture read or upload interrupted");
            return cached;
        }
        /* The invalid entry is replaced atomically after source decoding. */
    }
    if (!stable) writer.target[0] = '\0';
    BongoCatResult result = bongo_cat_image_decode_png_scaled_rows(source,
        max_width, max_height, cache_rows, &writer, progress, userdata,
        cancelled, cancel_data, error);
    uint64_t after_size = 0, after_time = 0;
    bool unchanged = stable &&
        bongo_cat_path_file_info(source, &after_size, &after_time) &&
        before_size == after_size && before_time == after_time;
    if (stable && !unchanged) writer.skip_reason = "source-changed";
    bool saved = result == BONGO_CAT_OK && !stopped(cancelled, cancel_data) &&
        unchanged && finish_write(&writer);
    discard(&writer);
    if (configured && result == BONGO_CAT_OK)
        SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
            "[texture-cache] result=%s bound=%dx%d elapsed_ms=%.1f reason=%s",
            saved ? "stored" : "uncached", max_width, max_height,
            (double)(SDL_GetTicksNS() - started) / 1000000.0,
            saved ? "ready" : writer.skip_reason);
    return result;
}
