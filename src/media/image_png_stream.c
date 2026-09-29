#include "image_internal.h"
#include "bongo_cat/file.h"

#include <limits.h>
#include <miniz.h>
#include <stdlib.h>
#include <string.h>

typedef struct PngRows {
    mz_stream inflater;
    unsigned char *line, *previous, *pixels;
    size_t line_size, line_used;
    int width, height, channels, batch, row, buffered;
    bool initialized, ended;
    BongoCatImageRows consume;
    void *consumer;
    BongoCatImageProgress progress;
    void *userdata;
    BongoCatImageCancelled cancelled;
    void *cancel_data;
} PngRows;

static bool stopped(const PngRows *png) {
    return png->cancelled && png->cancelled(png->cancel_data);
}

static uint32_t big_endian(const unsigned char *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
        (uint32_t)bytes[2] << 8 | bytes[3];
}

static bool read_exact(FILE *file, unsigned char *bytes, size_t size) {
    return fread(bytes, 1, size, file) == size;
}

static bool read_crc(FILE *file, mz_ulong calculated) {
    unsigned char crc[4];
    return read_exact(file, crc, sizeof(crc)) &&
        big_endian(crc) == (uint32_t)calculated;
}

static bool chunk_header(FILE *file, unsigned char *header) {
    if (!read_exact(file, header, 8) || big_endian(header) > INT_MAX) return false;
    for (int i = 4; i < 8; ++i)
        if (!((header[i] >= 'A' && header[i] <= 'Z') ||
              (header[i] >= 'a' && header[i] <= 'z'))) return false;
    return !(header[6] & 32); // PNG's reserved bit must be zero.
}

static int paeth(int left, int above, int corner) {
    int p = left + above - corner;
    int a = abs(p - left), b = abs(p - above), c = abs(p - corner);
    return a <= b && a <= c ? left : b <= c ? above : corner;
}

static void unfilter_row(PngRows *png) {
    unsigned char *row = png->line + 1;
    const unsigned char *above = png->previous + 1;
    size_t bytes = png->line_size - 1;
    size_t channels = (size_t)png->channels;
    /* A PNG filter is constant for the whole row. Select it once, and
       handle the first pixel separately to avoid per-byte boundary tests.
       Keep PNG's integer arithmetic and byte wrapping unchanged. */
    switch (png->line[0]) {
    case 0:
        break;
    case 1:
        for (size_t x = channels; x < bytes; ++x)
            row[x] = (unsigned char)(row[x] + row[x - channels]);
        break;
    case 2:
        for (size_t x = 0; x < bytes; ++x)
            row[x] = (unsigned char)(row[x] + above[x]);
        break;
    case 3:
        for (size_t x = 0; x < channels; ++x)
            row[x] = (unsigned char)(row[x] + above[x] / 2);
        for (size_t x = channels; x < bytes; ++x)
            row[x] = (unsigned char)(row[x] +
                (row[x - channels] + above[x]) / 2);
        break;
    case 4:
        for (size_t x = 0; x < channels; ++x)
            row[x] = (unsigned char)(row[x] + above[x]);
        for (size_t x = channels; x < bytes; ++x)
            row[x] = (unsigned char)(row[x] +
                paeth(row[x - channels], above[x], above[x - channels]));
        break;
    }
}

static bool finish_row(PngRows *png) {
    if (png->row >= png->height || png->line[0] > 4) return false;
    unfilter_row(png);
    unsigned char *row = png->line + 1;
    size_t bytes = png->line_size - 1;
    unsigned char *destination = png->pixels +
        (size_t)png->buffered * png->width * 4;
    if (png->channels == 4) memcpy(destination, row, bytes);
    else for (int x = 0; x < png->width; ++x) {
        memcpy(destination + (size_t)x * 4, row + (size_t)x * 3, 3);
        destination[(size_t)x * 4 + 3] = 255;
    }
    /* Retain the unfiltered source row by exchanging the two line buffers.
       The consumer owns a separate RGBA strip and may modify it. This avoids
       copying every source row again solely for the next PNG prediction. */
    unsigned char *previous = png->previous;
    png->previous = png->line;
    png->line = previous;
    ++png->row;
    ++png->buffered;
    if (png->buffered == png->batch || png->row == png->height) {
        BongoCatImage rows = {.pixels = png->pixels,
            .width = png->width, .height = png->buffered};
        if (!png->consume(png->consumer, &rows,
            png->height, png->row - png->buffered)) return false;
        png->buffered = 0;
        if (png->progress)
            png->progress(png->userdata, (float)png->row / png->height);
    }
    return true;
}

static bool inflate_bytes(PngRows *png, const unsigned char *data, unsigned count) {
    if (png->ended) return count == 0;
    png->inflater.next_in = data;
    png->inflater.avail_in = count;
    for (;;) {
        if (stopped(png)) return false;
        png->inflater.next_out = png->line + png->line_used;
        png->inflater.avail_out = (unsigned)(png->line_size - png->line_used);
        unsigned before_in = png->inflater.avail_in;
        unsigned before_out = png->inflater.avail_out;
        int status = mz_inflate(&png->inflater, MZ_NO_FLUSH);
        png->line_used += before_out - png->inflater.avail_out;
        if (png->line_used == png->line_size) {
            if (!finish_row(png)) return false;
            png->line_used = 0;
        }
        if (status == MZ_STREAM_END) {
            png->ended = true;
            return !png->inflater.avail_in && !png->line_used &&
                png->row == png->height;
        }
        if (status != MZ_OK && status != MZ_BUF_ERROR) return false;
        if (before_in == png->inflater.avail_in && before_out == png->inflater.avail_out)
            return png->inflater.avail_in == 0;
    }
}

static bool decode_png(FILE *file, PngRows *png) {
    static const unsigned char signature[] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    unsigned char header[8], info[13];
    if (stopped(png) || !read_exact(file, header, sizeof(header)) ||
        memcmp(header, signature, sizeof(signature)) ||
        !chunk_header(file, header) || big_endian(header) != sizeof(info) ||
        memcmp(header + 4, "IHDR", 4) || !read_exact(file, info, sizeof(info))) return false;
    mz_ulong crc = mz_crc32(0, header + 4, 4);
    if (!read_crc(file, mz_crc32(crc, info, sizeof(info)))) return false;
    uint32_t width = big_endian(info), height = big_endian(info + 4);
    if (!width || !height || width > INT_MAX / 4 || height > INT_MAX ||
        info[8] != 8 || (info[9] != 2 && info[9] != 6) ||
        info[10] || info[11] || info[12]) return false;
    png->width = (int)width;
    png->height = (int)height;
    png->channels = info[9] == 6 ? 4 : 3;
    png->line_size = (size_t)width * png->channels + 1;
    size_t stride = (size_t)width * 4;
    size_t batch = 4u * 1024u * 1024u / stride;
    png->batch = (int)(batch < 1 ? 1 : batch > 64 ? 64 : batch);
    if (png->batch > png->height) png->batch = png->height;
    png->line = malloc(png->line_size);
    png->previous = calloc(1, png->line_size);
    png->pixels = malloc(stride * png->batch);
    if (!png->line || !png->previous || !png->pixels) return false;
    if (mz_inflateInit(&png->inflater) != MZ_OK) return false;
    png->initialized = true;

    bool idat_seen = false, idat_closed = false, palette_seen = false;
    unsigned char input[32768];
    while (!stopped(png) && chunk_header(file, header)) {
        uint32_t length = big_endian(header);
        bool idat = memcmp(header + 4, "IDAT", 4) == 0;
        bool end = memcmp(header + 4, "IEND", 4) == 0;
        bool palette = memcmp(header + 4, "PLTE", 4) == 0;
        if (idat) {
            if (idat_closed) return false;
            idat_seen = true;
        } else {
            if (idat_seen) {
                idat_closed = true;
                if (!png->ended) return false;
            }
            if (end && length) return false;
            if (palette) {
                if (palette_seen || idat_seen || !length || length > 768 || length % 3)
                    return false;
                palette_seen = true;
            } else if (!end && (!(header[4] & 32) || !memcmp(header + 4, "tRNS", 4))) {
                // Unknown critical chunks and color-key alpha require a general decoder.
                return false;
            }
        }
        crc = mz_crc32(0, header + 4, 4);
        while (length) {
            if (stopped(png)) return false;
            unsigned count = length < sizeof(input) ? length : (unsigned)sizeof(input);
            if (!read_exact(file, input, count)) return false;
            crc = mz_crc32(crc, input, count);
            if (idat) {
                bool inflated = inflate_bytes(png, input, count);
                png->inflater.next_in = NULL;
                png->inflater.avail_in = 0;
                if (!inflated) return false;
            }
            length -= count;
            if (!idat && png->progress)
                png->progress(png->userdata, (float)png->row / png->height);
        }
        if (!read_crc(file, crc)) return false;
        if (end)
            return idat_seen && png->ended && fgetc(file) == EOF && !ferror(file);
    }
    return false;
}

bool bongo_cat_image_decode_png_rows(const char *path,
    BongoCatImageRows consume, void *consumer,
    BongoCatImageProgress progress, void *userdata) {
    return bongo_cat_image_decode_png_rows_cancellable(path, consume, consumer,
        progress, userdata, NULL, NULL);
}

bool bongo_cat_image_decode_png_rows_cancellable(const char *path,
    BongoCatImageRows consume, void *consumer,
    BongoCatImageProgress progress, void *userdata,
    BongoCatImageCancelled cancelled, void *cancel_data) {
    if (!path || !consume) return false;
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    PngRows png = {.consume = consume, .consumer = consumer,
        .progress = progress, .userdata = userdata,
        .cancelled = cancelled, .cancel_data = cancel_data};
    bool result = decode_png(file, &png);
    if (png.initialized) mz_inflateEnd(&png.inflater);
    free(png.line);
    free(png.previous);
    free(png.pixels);
    fclose(file);
    return result;
}
