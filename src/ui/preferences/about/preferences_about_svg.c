#include "preferences_about_svg.h"
#include <math.h>
#include <stdlib.h>
#define NANOSVG_IMPLEMENTATION
#include "../../../../vendor/nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../../../../vendor/nanosvg/nanosvgrast.h"

static unsigned char *svg_pixels(char *svg, int size) {
    NSVGimage *image = nsvgParse(svg, "px", 96);
    if (!image)
        return NULL;
    unsigned char *pixels = NULL;
    if (isfinite(image->width) && isfinite(image->height) && image->width > 0 &&
        image->height > 0 && image->width <= 4096 && image->height <= 4096) {
        NSVGrasterizer *raster = nsvgCreateRasterizer();
        pixels = calloc((size_t)size * (size_t)size, 4);
        if (raster && pixels) {
            float scale = (float)size / fmaxf(image->width, image->height);
            nsvgRasterize(raster, image, ((float)size - image->width * scale) * .5f,
                          ((float)size - image->height * scale) * .5f, scale, pixels, size, size, size * 4);
        } else {
            free(pixels);
            pixels = NULL;
        }
        nsvgDeleteRasterizer(raster);
    }
    nsvgDelete(image);
    return pixels;
}

unsigned char *bongo_cat_about_qr_pixels(char *svg) {
    return svg_pixels(svg, 240);
}

unsigned char *bongo_cat_about_wechat_pixels(int size) {
    /* Exact two-bubble WeChat paths from the supplied 1.html, rasterized once. */
    char svg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\">"
        "<path fill=\"white\" d=\"M9.5 3C4.8 3 1 6 1 9.7c0 2.1 1.2 4 3.2 5.2L3.4 18l3.4-1.7c.9.2 1.8.3 2.7.3h.5a6.8 6.8 0 0 1-.8-3.2c0-3.7 3.5-6.7 7.8-6.7h.3C15.9 4.5 12.9 3 9.5 3ZM6.6 6.8a1.1 1.1 0 1 1 0 2.2 1.1 1.1 0 0 1 0-2.2Zm5.8 0a1.1 1.1 0 1 1 0 2.2 1.1 1.1 0 0 1 0-2.2Z\"/>"
        "<path fill=\"white\" d=\"M17 8c-3.6 0-6.5 2.4-6.5 5.4s2.9 5.4 6.5 5.4c.7 0 1.4-.1 2.1-.3l2.6 1.3-.6-2.4c1.5-1 2.4-2.4 2.4-4C23.5 10.4 20.6 8 17 8Zm-2.3 3a.9.9 0 1 1 0 1.8.9.9 0 0 1 0-1.8Zm4.6 0a.9.9 0 1 1 0 1.8.9.9 0 0 1 0-1.8Z\"/></svg>";
    if (size < 25 || size > 100) return NULL;
    return svg_pixels(svg, size);
}

int bongo_cat_about_svg_parse_xml(char *input,
    void (*start)(void *, const char *, const char **),
    void (*end)(void *, const char *), void (*content)(void *, const char *), void *user) {
    return nsvg__parseXML(input, start, end, content, user);
}
