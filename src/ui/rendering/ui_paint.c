#include "ui_paint.h"
#include "ui_backend.h"
#include "ui_paint_border.h"
#include "ui_paint_cache.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define BONGO_CAT_UI_PAINT_SCALE_MAX 2.25f

static float clamp01(float value) {
    return NK_CLAMP(0.0f, value, 1.0f);
}
struct nk_color bongo_cat_ui_color_mix(struct nk_color first,
    struct nk_color second, float amount) {
    amount = clamp01(amount);
    return nk_rgba((nk_byte)(first.r + (second.r - first.r) * amount + .5f),
        (nk_byte)(first.g + (second.g - first.g) * amount + .5f),
        (nk_byte)(first.b + (second.b - first.b) * amount + .5f),
        (nk_byte)(first.a + (second.a - first.a) * amount + .5f));
}

struct nk_color bongo_cat_ui_color_alpha(struct nk_color color,
    float amount) {
    color.a = (nk_byte)(color.a * clamp01(amount) + .5f);
    return color;
}

static uint32_t pack(struct nk_color color) {
    return (uint32_t)color.r | (uint32_t)color.g << 8 |
        (uint32_t)color.b << 16 | (uint32_t)color.a << 24;
}

static void pixel(unsigned char *target, struct nk_color color, float alpha) {
    unsigned char coverage = (unsigned char)(color.a * clamp01(alpha) + .5f);
    /* GL_LINEAR interpolates RGB and alpha independently. Keeping a colored
       RGB value in a fully transparent texel can therefore create a visible
       fringe on DWM-composited windows when a neighbouring sample is mixed. */
    target[0] = coverage ? color.r : 0; target[1] = coverage ? color.g : 0; target[2] = coverage ? color.b : 0;
    target[3] = coverage;
}

static float rounded_distance(float x, float y, float width, float height,
    float radius) {
    radius = NK_CLAMP(0.0f, radius, NK_MIN(width, height) * .5f);
    float qx = fabsf(x - width * .5f) - (width * .5f - radius);
    float qy = fabsf(y - height * .5f) - (height * .5f - radius);
    float outside_x = NK_MAX(qx, 0.0f), outside_y = NK_MAX(qy, 0.0f);
    return sqrtf(outside_x * outside_x + outside_y * outside_y) +
        NK_MIN(NK_MAX(qx, qy), 0.0f) - radius;
}

static bool texture_dimensions(struct nk_context *context,
    struct nk_rect bounds, int *width, int *height, float *scale_x,
    float *scale_y, BongoCatUIBackend **backend) {
    *backend = bongo_cat_ui_backend_for_context(context);
    if (!*backend || !(*backend)->window) return false;
    float logical_width = 0.0f, logical_height = 0.0f;
    int pixel_width, pixel_height;
    bongo_cat_ui_logical_size(*backend, &logical_width, &logical_height);
    SDL_GetWindowSizeInPixels((*backend)->window, &pixel_width, &pixel_height);
    if (logical_width < 1.0f || logical_height < 1.0f) return false;
    *scale_x = NK_MIN(pixel_width / logical_width,
        BONGO_CAT_UI_PAINT_SCALE_MAX);
    *scale_y = NK_MIN(pixel_height / logical_height,
        BONGO_CAT_UI_PAINT_SCALE_MAX);
    *width = NK_MAX(1, (int)ceilf(bounds.w * *scale_x));
    *height = NK_MAX(1, (int)ceilf(bounds.h * *scale_y));
    return true;
}

void bongo_cat_ui_paint_gradient(struct nk_context *context,
    struct nk_rect bounds, float rounding, struct nk_color first,
    struct nk_color second) {
    int width, height; float sx, sy; BongoCatUIBackend *backend;
    if (!texture_dimensions(context, bounds, &width, &height, &sx, &sy,
        &backend)) {
        nk_fill_rect(nk_window_get_canvas(context), bounds, rounding,
            bongo_cat_ui_color_mix(first, second, .5f)); return;
    }
    BongoCatUIPaintKey key = {BONGO_CAT_UI_PAINT_GRADIENT, width, height,
        (int)lroundf(rounding * (sx + sy) * .5f), 0, 0,
        pack(first), pack(second)};
    BongoCatUIPaintTexture *item =
        bongo_cat_ui_paint_cache_get(backend, &key);
    if (!item) return;
    if (!bongo_cat_ui_paint_cache_ready(item)) {
        size_t bytes = (size_t)width * height * 4;
        unsigned char *pixels = malloc(bytes); if (!pixels) return;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            float amount = ((x + .5f) / width + (y + .5f) / height) * .5f;
            float distance = rounded_distance(x + .5f, y + .5f,
                (float)width, (float)height, (float)key.radius);
            pixel(pixels + ((size_t)y * width + x) * 4,
                bongo_cat_ui_color_mix(first, second, amount), .5f - distance);
        }
        if (!bongo_cat_ui_paint_cache_upload(item, pixels, false)) {
            free(pixels); return;
        }
        free(pixels);
    }
    bongo_cat_ui_paint_cache_draw(context, bounds, item,
        nk_rgb(255, 255, 255));
}
static void paint_radial(struct nk_context *context, int kind,
    struct nk_rect bounds, struct nk_color center, struct nk_color edge,
    float midpoint, float outer) {
    int width, height; float sx, sy; BongoCatUIBackend *backend;
    if (!texture_dimensions(context, bounds, &width, &height, &sx, &sy,
        &backend)) return;
    BongoCatUIPaintKey key = {kind, width, height, 0,
        (int)lroundf(midpoint * 1000), (int)lroundf(outer * 1000),
        pack(center), pack(edge)};
    bool single_channel = key.first_color == key.second_color;
    BongoCatUIPaintTexture *item =
        bongo_cat_ui_paint_cache_get(backend, &key);
    if (!item) return;
    if (!bongo_cat_ui_paint_cache_ready(item)) {
        unsigned char *pixels = malloc((size_t)width * height *
            (single_channel ? 1 : 4));
        if (!pixels) return;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            float dx = (2.0f * (x + .5f) - width) / width;
            float dy = (2.0f * (y + .5f) - height) /
                (kind == BONGO_CAT_UI_PAINT_RADIAL_CIRCLE ? width : height);
            float distance = sqrtf(dx * dx + dy * dy);
            float mix = midpoint > 0 ? clamp01(distance / midpoint) : 1.0f;
            float fade = outer > midpoint ?
                clamp01((outer - distance) / (outer - midpoint)) : 0.0f;
            fade = fade * fade * (3.0f - 2.0f * fade);
            float alpha = distance <= midpoint ? 1.0f : fade;
            size_t offset = (size_t)y * width + x;
            if (single_channel)
                pixels[offset] = (unsigned char)(center.a * clamp01(alpha) + .5f);
            else pixel(pixels + offset * 4,
                bongo_cat_ui_color_mix(center, edge, mix), alpha);
        }
        if (!bongo_cat_ui_paint_cache_upload(item, pixels, single_channel)) {
            free(pixels); return;
        }
        free(pixels);
    }
    bongo_cat_ui_paint_cache_draw(context, bounds, item, single_channel ?
        nk_rgb(center.r, center.g, center.b) : nk_rgb(255, 255, 255));
}

void bongo_cat_ui_paint_radial(struct nk_context *context,
    struct nk_rect bounds, struct nk_color center, struct nk_color edge,
    float midpoint, float outer) {
    paint_radial(context, BONGO_CAT_UI_PAINT_RADIAL, bounds, center, edge,
        midpoint, outer);
}

void bongo_cat_ui_paint_radial_circle(struct nk_context *context,
    struct nk_rect bounds, struct nk_color center, struct nk_color edge,
    float midpoint, float outer) {
    paint_radial(context, BONGO_CAT_UI_PAINT_RADIAL_CIRCLE, bounds, center,
        edge, midpoint, outer);
}

void bongo_cat_ui_paint_sidebar_glow(struct nk_context *context,
    struct nk_rect surface, float sidebar, float rounding,
    struct nk_color color) {
    int width, height; float sx, sy; BongoCatUIBackend *backend;
    if (!texture_dimensions(context, surface, &width, &height, &sx, &sy,
        &backend)) return;
    float scale = (sx + sy) * .5f;
    BongoCatUIPaintKey key = {BONGO_CAT_UI_PAINT_SIDEBAR_GLOW,
        width, height, (int)lroundf(rounding * scale),
        (int)lroundf(sidebar * sx), 0, pack(color), 0};
    BongoCatUIPaintTexture *item = bongo_cat_ui_paint_cache_get(backend, &key);
    if (!item) return;
    if (!bongo_cat_ui_paint_cache_ready(item)) {
        unsigned char *pixels = malloc((size_t)width * height);
        if (!pixels) return;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            float logical_x = (x + .5f) / sx, logical_y = (y + .5f) / sy;
            float dx = (logical_x - 12.0f) / 210.0f;
            float dy = (logical_y - 12.0f) / 210.0f;
            float distance = sqrtf(dx * dx + dy * dy);
            float fade = clamp01((1.1f - distance) / .75f);
            fade = fade * fade * (3.0f - 2.0f * fade);
            float radial = distance <= .35f ? 1.0f : fade;
            float rounded = .5f - rounded_distance(x + .5f, y + .5f,
                (float)width, (float)height, (float)key.radius);
            float sidebar_clip = key.first_parameter + .5f - (x + .5f);
            float mask = clamp01(NK_MIN(rounded, sidebar_clip));
            pixels[(size_t)y * width + x] =
                (unsigned char)(color.a * radial * mask + .5f);
        }
        if (!bongo_cat_ui_paint_cache_upload(item, pixels, true)) {
            free(pixels); return;
        }
        free(pixels);
    }
    bongo_cat_ui_paint_cache_draw(context, surface, item,
        nk_rgb(color.r, color.g, color.b));
}

void bongo_cat_ui_paint_shadow(struct nk_context *context,
    struct nk_rect bounds, float rounding, float offset_x, float offset_y,
    float blur, float spread, struct nk_color color) {
    float pad = ceilf(blur * 1.5f + spread + 2.0f);
    struct nk_rect target = nk_rect(bounds.x + offset_x - pad,
        bounds.y + offset_y - pad, bounds.w + pad * 2, bounds.h + pad * 2);
    int width, height; float sx, sy; BongoCatUIBackend *backend;
    if (!texture_dimensions(context, target, &width, &height, &sx, &sy,
        &backend)) return;
    float scale = (sx + sy) * .5f;
    struct nk_color tint = color;
    BongoCatUIPaintKey key = {BONGO_CAT_UI_PAINT_SHADOW, width, height,
        (int)lroundf(rounding * scale), (int)lroundf(blur * scale),
        (int)lroundf(spread * scale), pack(nk_rgb(255, 255, 255)), 0};
    BongoCatUIPaintTexture *item =
        bongo_cat_ui_paint_cache_get(backend, &key);
    if (!item) return;
    if (!bongo_cat_ui_paint_cache_ready(item)) {
        unsigned char *pixels = malloc((size_t)width * height);
        if (!pixels) return;
        float pad_x = pad * sx, pad_y = pad * sy;
        float shape_w = bounds.w * sx + 2.0f * key.second_parameter;
        float shape_h = bounds.h * sy + 2.0f * key.second_parameter;
        float center_x = pad_x + bounds.w * sx * .5f;
        float center_y = pad_y + bounds.h * sy * .5f;
        float sigma = NK_MAX(1.0f, (float)key.first_parameter * .5f);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            float local_x = x + .5f - center_x + shape_w * .5f;
            float local_y = y + .5f - center_y + shape_h * .5f;
            float distance = rounded_distance(local_x, local_y,
                shape_w, shape_h,
                (float)(key.radius + key.second_parameter));
            float alpha = .55f *
                expf(-.5f * distance * distance / (sigma * sigma));
            pixels[(size_t)y * width + x] =
                (unsigned char)(255.0f * clamp01(alpha) + .5f);
        }
        if (!bongo_cat_ui_paint_cache_upload(item, pixels, true)) {
            free(pixels); return;
        }
        free(pixels);
    }
    bongo_cat_ui_paint_cache_draw(context, target, item, tint);
}

void bongo_cat_ui_paint_dashed_rounded(struct nk_context *context,
    struct nk_rect bounds, float rounding, float thickness, float dash,
    float gap, struct nk_color color) {
    int width, height; float sx, sy; BongoCatUIBackend *backend;
    if (!texture_dimensions(context, bounds, &width, &height, &sx, &sy,
        &backend)) {
        nk_stroke_rect(nk_window_get_canvas(context), bounds, rounding,
            thickness, color); return;
    }
    float scale = (sx + sy) * .5f;
    int gap_and_thickness = NK_CLAMP(1, (int)lroundf(gap * scale), 32767) << 16 |
        NK_CLAMP(1, (int)lroundf(thickness * scale), 65535);
    BongoCatUIPaintKey key = {BONGO_CAT_UI_PAINT_DASHED_ROUNDED,
        width, height,
        (int)lroundf(rounding * scale),
        NK_MAX(1, (int)lroundf(dash * scale)), gap_and_thickness,
        pack(color), 0};
    BongoCatUIPaintTexture *item =
        bongo_cat_ui_paint_cache_get(backend, &key);
    if (!item) return;
    if (!bongo_cat_ui_paint_cache_ready(item)) {
        unsigned char *pixels = malloc((size_t)width * height * 4);
        if (!pixels) return;
        bongo_cat_ui_raster_dashed_rounded(pixels, width, height,
            (float)key.radius, (float)(key.second_parameter & 65535),
            (float)key.first_parameter, (float)(key.second_parameter >> 16),
            color);
        if (!bongo_cat_ui_paint_cache_upload(item, pixels, false)) {
            free(pixels); return;
        }
        free(pixels);
    }
    bongo_cat_ui_paint_cache_draw(context, bounds, item,
        nk_rgb(255, 255, 255));
}
