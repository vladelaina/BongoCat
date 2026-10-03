#include "image_internal.h"
#include "bongo_cat/file.h"
#include "test.h"
#include <SDL3/SDL.h>
#include <miniz.h>
#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;

typedef enum FixtureFault {
    VALID, BAD_IHDR_CRC, BAD_IDAT_CRC, TRUNCATED_IDAT, BAD_FILTER, BAD_ADLER,
    SHORT_PIXELS, EXTRA_PIXELS, MISSING_IEND, COLOR_KEY, INTERLACED,
    SIXTEEN_BIT, UNKNOWN_CRITICAL, DISJOINT_IDAT, OVERFLOW_SIZE, EXTRA_IDAT
} FixtureFault;

static void put_u32(unsigned char *bytes, uint32_t value) {
    bytes[0] = (unsigned char)(value >> 24);
    bytes[1] = (unsigned char)(value >> 16);
    bytes[2] = (unsigned char)(value >> 8);
    bytes[3] = (unsigned char)value;
}

static void chunk(FILE *file, const char *name, const unsigned char *bytes,
    size_t length, bool corrupt) {
    unsigned char encoded[4];
    put_u32(encoded, (uint32_t)length);
    CHECK(fwrite(encoded, 1, 4, file) == 4);
    CHECK(fwrite(name, 1, 4, file) == 4);
    CHECK(!length || fwrite(bytes, 1, length, file) == length);
    mz_ulong crc = mz_crc32(0, (const unsigned char *)name, 4);
    crc = mz_crc32(crc, bytes, length);
    put_u32(encoded, (uint32_t)crc ^ (corrupt ? 1u : 0u));
    CHECK(fwrite(encoded, 1, 4, file) == 4);
}

static unsigned char pixel(int x, int y, int channel) {
    uint32_t value = (uint32_t)x * 0x61c88647u ^ (uint32_t)y * 0x9e3779b9u ^
        (uint32_t)channel * 0x85ebca6bu;
    value ^= value >> 16;
    value *= 0x7feb352du;
    return (unsigned char)(value ^ (value >> 13));
}

static int predict(int kind, int left, int up, int corner) {
    if (kind == 0) return 0;
    if (kind == 1) return left;
    if (kind == 2) return up;
    if (kind == 3) return (left + up) / 2;
    int a = abs(up - corner), b = abs(left - corner), c = abs(left + up - 2 * corner);
    return a <= b && a <= c ? left : b <= c ? up : corner;
}

static void fixture(const char *path, int width, int height, int channels,
    int filter, FixtureFault fault) {
    FILE *file = bongo_cat_file_open(path, "wb");
    CHECK(file != NULL);
    if (!file) return;
    static const unsigned char signature[] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    CHECK(fwrite(signature, 1, sizeof(signature), file) == sizeof(signature));
    unsigned char ihdr[13] = {0};
    put_u32(ihdr, fault == OVERFLOW_SIZE ? UINT32_MAX : (uint32_t)width);
    put_u32(ihdr + 4, (uint32_t)height);
    ihdr[8] = fault == SIXTEEN_BIT ? 16 : 8;
    ihdr[9] = channels == 4 ? 6 : 2;
    ihdr[12] = fault == INTERLACED ? 1 : 0;
    chunk(file, "IHDR", ihdr, sizeof(ihdr), fault == BAD_IHDR_CRC);
    unsigned char empty[6] = {0};
    if (fault == COLOR_KEY) chunk(file, "tRNS", empty, sizeof(empty), false);
    if (fault == UNKNOWN_CRITICAL) chunk(file, "ABCD", empty, sizeof(empty), false);
    int rows = height + (fault == SHORT_PIXELS ? -1 : fault == EXTRA_PIXELS ? 1 : 0);
    size_t row_bytes = (size_t)width * channels, stride = row_bytes + 1;
    size_t raw_size = stride * rows;
    unsigned char *raw = malloc(raw_size);
    CHECK(raw != NULL);
    if (!raw) { fclose(file); return; }
    for (int y = 0; y < rows; ++y) {
        int kind = filter < 0 ? y % 5 : filter;
        raw[(size_t)y * stride] = (unsigned char)(fault == BAD_FILTER && y == 0 ? 5 : kind);
        for (size_t i = 0; i < row_bytes; ++i) {
            int x = (int)(i / channels), channel = (int)(i % channels);
            int left = x ? pixel(x - 1, y, channel) : 0;
            int up = y ? pixel(x, y - 1, channel) : 0;
            int corner = x && y ? pixel(x - 1, y - 1, channel) : 0;
            raw[(size_t)y * stride + i + 1] =
                (unsigned char)(pixel(x, y, channel) - predict(kind, left, up, corner));
        }
    }
    mz_ulong compressed_size = mz_compressBound((mz_ulong)raw_size);
    unsigned char *compressed = malloc(compressed_size);
    CHECK(compressed != NULL);
    if (!compressed) { free(raw); fclose(file); return; }
    CHECK(mz_compress2(compressed, &compressed_size, raw, (mz_ulong)raw_size, 6) == MZ_OK);
    free(raw);
    if (fault == BAD_ADLER) compressed[compressed_size - 1] ^= 1;
    if (fault == TRUNCATED_IDAT) {
        unsigned char length[4];
        put_u32(length, (uint32_t)compressed_size);
        CHECK(fwrite(length, 1, 4, file) == 4);
        CHECK(fwrite("IDAT", 1, 4, file) == 4);
        CHECK(fwrite(compressed, 1, compressed_size - 3, file) == compressed_size - 3);
    } else {
        // Empty chunks, a split zlib header, split rows and input-buffer crossings.
        chunk(file, "IDAT", empty, 0, false);
        size_t offset = 0;
        const size_t batches[] = {1, 7, 4099, 65537};
        for (size_t batch = 0; offset < compressed_size; ++batch) {
            size_t count = batches[batch % SDL_arraysize(batches)];
            if (count > compressed_size - offset) count = compressed_size - offset;
            chunk(file, "IDAT", compressed + offset, count, fault == BAD_IDAT_CRC && batch == 1);
            offset += count;
            if (fault == DISJOINT_IDAT && batch == 0)
                chunk(file, "tEXt", (const unsigned char *)"x\0y", 3, false);
        }
        chunk(file, "IDAT", empty, fault == EXTRA_IDAT ? 1 : 0, false);
        if (fault != MISSING_IEND) chunk(file, "IEND", empty, 0, false);
    }
    free(compressed);
    CHECK(fclose(file) == 0);
}

typedef struct Consumer {
    int width, height, channels, row, calls, cancel_at;
    bool wrong_pixel;
    float last_progress;
} Consumer;

static bool consume(void *userdata, BongoCatImage *rows, int height, int y) {
    Consumer *state = userdata;
    CHECK(rows->width == state->width && height == state->height && y == state->row);
    CHECK(rows->height > 0 && rows->height <= 64 && y + rows->height <= height);
    for (int row = 0; row < rows->height; ++row)
        for (int x = 0; x < rows->width; ++x)
            for (int channel = 0; channel < 4; ++channel) {
                unsigned char expected = channel == 3 && state->channels == 3 ?
                    255 : pixel(x, y + row, channel);
                if (rows->pixels[((size_t)row * rows->width + x) * 4 + channel] != expected)
                    state->wrong_pixel = true;
            }
    state->row += rows->height;
    ++state->calls;
    // Model uploads premultiply in place. This must not corrupt the next filter row.
    memset(rows->pixels, 0, (size_t)rows->width * rows->height * 4);
    return !state->cancel_at || state->calls < state->cancel_at;
}

static void progress(void *userdata, float fraction) {
    Consumer *state = userdata;
    CHECK(fraction >= state->last_progress && fraction <= 1.0f);
    state->last_progress = fraction;
}

static void run_case(const char *path, int width, int height, int channels,
    int filter, FixtureFault fault, int cancel_at) {
    fixture(path, width, height, channels, filter, fault);
    Consumer state = {.width = width, .height = height,
        .channels = channels, .cancel_at = cancel_at};
    bool result = bongo_cat_image_decode_png_rows(path, consume, &state, progress, &state);
    CHECK(result == (fault == VALID && !cancel_at));
    if (fault != BAD_FILTER) CHECK(!state.wrong_pixel);
    if (result) CHECK(state.row == height && state.last_progress == 1.0f);
    if (cancel_at) CHECK(state.calls == cancel_at);
    if (fault == COLOR_KEY || fault == INTERLACED || fault == SIXTEEN_BIT ||
        fault == UNKNOWN_CRITICAL || fault == OVERFLOW_SIZE || fault == BAD_IHDR_CRC)
        CHECK(state.calls == 0);
}

int main(void) {
    char *path = NULL;
    SDL_asprintf(&path, "%sbongocat-png-stream-%llu.png", SDL_GetBasePath(),
        (unsigned long long)SDL_GetTicksNS());
    CHECK(path != NULL);
    if (!path) return 1;
    const int sizes[][2] = {{1, 1}, {1, 129}, {193, 131}, {8192, 65}};
    for (size_t i = 0; i < SDL_arraysize(sizes); ++i)
        for (int channels = 3; channels <= 4; ++channels)
            for (int filter = -1; filter < 5; ++filter)
                run_case(path, sizes[i][0], sizes[i][1], channels, filter, VALID, 0);
    for (int fault = BAD_IHDR_CRC; fault <= EXTRA_IDAT; ++fault)
        run_case(path, 193, 131, 4, -1, (FixtureFault)fault, 0);
    run_case(path, 193, 257, 4, -1, VALID, 1);
    run_case(path, 193, 257, 3, -1, VALID, 2);
    CHECK(SDL_RemovePath(path));
    SDL_free(path);
    return bongo_cat_test_failures ? 1 : 0;
}
