#include "image_texture_cache.h"
#include "test.h"

#include <SDL3/SDL.h>
#include <miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;
static size_t allocated_bytes, read_bytes;
static unsigned seeks;

static void *counted_malloc(size_t size) {
    allocated_bytes += size;
    return malloc(size);
}

static size_t counted_read(void *buffer, size_t size, size_t count, FILE *file) {
    size_t result = fread(buffer, size, count, file);
    read_bytes += size * result;
    return result;
}

static int counted_seek(FILE *file, long offset, int origin) {
    ++seeks;
    return fseek(file, offset, origin);
}

/* Exercise the actual reader with file-backed fixtures. Count its allocation
   and I/O so a small entry cannot regress to a fixed buffer or second pass. */
#define malloc counted_malloc
#define fread counted_read
#define fseek counted_seek
#define bongo_cat_image_decode_cached_scaled_rows test_decode_cached_scaled_rows
#include "../../src/media/image_texture_cache.c"
#undef bongo_cat_image_decode_cached_scaled_rows
#undef fseek
#undef fread
#undef malloc

static const char fixture_digest[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

typedef enum CacheFault { CACHE_VALID, HEADER_CRC, PIXEL_CRC, SHORT_FILE, EXTRA_BYTE } CacheFault;

static unsigned char fixture_byte(size_t offset) {
    return (unsigned char)((offset * 37u + offset / 251u) & 255u);
}

static FILE *cache_fixture(int width, int height, CacheFault fault) {
    FILE *file = tmpfile();
    CHECK(file != NULL);
    if (!file) return NULL;
    unsigned char header[96] = {'B', 'C', 'R', 'G', 'B', 'A', '0', '1'};
    put32(header + 8, (uint32_t)width); put32(header + 12, (uint32_t)height);
    put32(header + 16, (uint32_t)width); put32(header + 20, (uint32_t)height);
    memcpy(header + 32, fixture_digest, 64);
    CHECK(fwrite(header, 1, sizeof(header), file) == sizeof(header));
    size_t bytes = (size_t)width * height * 4;
    unsigned char block[4096];
    mz_ulong crc = 0;
    for (size_t offset = 0; offset < bytes;) {
        size_t count = SDL_min(sizeof(block), bytes - offset);
        for (size_t i = 0; i < count; ++i) block[i] = fixture_byte(offset + i);
        crc = mz_crc32(crc, block, count);
        if (fault == PIXEL_CRC && !offset) block[0] ^= 1;
        size_t written = count - (fault == SHORT_FILE && offset + count == bytes ? 1 : 0);
        CHECK(fwrite(block, 1, written, file) == written);
        offset += count;
    }
    if (fault == EXTRA_BYTE) CHECK(fputc(1, file) != EOF);
    put32(header + 24, (uint32_t)crc);
    put32(header + 28, (uint32_t)mz_crc32(0, header, sizeof(header)));
    if (fault == HEADER_CRC) header[28] ^= 1;
    CHECK(fseek(file, 0, SEEK_SET) == 0);
    CHECK(fwrite(header, 1, sizeof(header), file) == sizeof(header));
    rewind(file);
    return file;
}

typedef struct CacheConsumer {
    int width, height, row, calls;
    float progress;
    bool cancel_after_validation, cancelled, reject;
} CacheConsumer;

static bool consume_cache(void *userdata, BongoCatImage *rows, int height, int y) {
    CacheConsumer *state = userdata;
    CHECK(rows->width == state->width && height == state->height && y == state->row);
    CHECK(rows->height > 0 && rows->height <= height - y);
    size_t bytes = (size_t)rows->width * rows->height * 4;
    size_t start = (size_t)y * rows->width * 4;
    bool exact = true;
    for (size_t i = 0; i < bytes; ++i)
        if (rows->pixels[i] != fixture_byte(start + i)) exact = false;
    CHECK(exact);
    // The upload can mutate the emitted pixels; later strips must stay exact.
    memset(rows->pixels, 0, bytes);
    state->row += rows->height;
    ++state->calls;
    return !state->reject;
}

static void cache_progress(void *userdata, float progress) {
    CacheConsumer *state = userdata;
    CHECK(progress >= state->progress && progress <= 1.0f);
    state->progress = progress;
    if (state->cancel_after_validation && progress >= .1f) state->cancelled = true;
}

static bool cache_cancelled(void *userdata) {
    return ((CacheConsumer *)userdata)->cancelled;
}

static void check_cache(int width, int height, CacheFault fault, bool cancel, bool reject) {
    FILE *file = cache_fixture(width, height, fault);
    if (!file) return;
    CacheConsumer state = {.width = width, .height = height,
        .cancel_after_validation = cancel, .reject = reject};
    allocated_bytes = read_bytes = 0; seeks = 0;
    BongoCatResult result = read_cache(file, fixture_digest, width, height,
        consume_cache, &state, cache_progress, &state, cache_cancelled, &state);
    fclose(file);
    if (fault != CACHE_VALID) {
        CHECK(result == BONGO_CAT_ERROR_FORMAT && state.calls == 0);
        return;
    }
    size_t bytes = (size_t)width * height * 4;
    size_t stride = (size_t)width * 4;
    CHECK(allocated_bytes <= bytes);
    CHECK(allocated_bytes <= SDL_max((size_t)(1024 * 1024), stride));
    if (cancel || reject) {
        CHECK(result == BONGO_CAT_ERROR_PLATFORM);
        CHECK(state.calls == (reject ? 1 : 0));
    } else {
        CHECK(result == BONGO_CAT_OK && state.row == height && state.progress == 1.0f);
        bool single_strip = bytes <= 1024u * 1024u || height == 1;
        CHECK(read_bytes == 96u + bytes * (single_strip ? 1u : 2u));
        CHECK(seeks == (single_strip ? 0u : 1u));
        if (single_strip) CHECK(allocated_bytes == bytes && state.calls == 1);
    }
}

int main(void) {
    const int sizes[][2] = {{1, 1}, {130, 65}, {512, 512}, {513, 513},
        {8192, 1}, {1, 257}, {300000, 1}};
    for (size_t i = 0; i < SDL_arraysize(sizes); ++i) {
        check_cache(sizes[i][0], sizes[i][1], CACHE_VALID, false, false);
        check_cache(sizes[i][0], sizes[i][1], CACHE_VALID, true, false);
        check_cache(sizes[i][0], sizes[i][1], CACHE_VALID, false, true);
    }
    for (int fault = HEADER_CRC; fault <= EXTRA_BYTE; ++fault) {
        check_cache(130, 65, (CacheFault)fault, false, false);
        check_cache(513, 513, (CacheFault)fault, false, false);
    }
    CHECK(test_decode_cached_scaled_rows(NULL, fixture_digest, 1, 1,
        consume_cache, NULL, NULL, NULL, NULL, NULL, NULL) == BONGO_CAT_ERROR_ARGUMENT);
    CHECK(test_decode_cached_scaled_rows("unused", fixture_digest, 1, 1,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL) == BONGO_CAT_ERROR_ARGUMENT);
    return bongo_cat_test_failures ? 1 : 0;
}
