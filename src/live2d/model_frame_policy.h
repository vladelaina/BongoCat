#ifndef BONGO_CAT_MODEL_FRAME_POLICY_H
#define BONGO_CAT_MODEL_FRAME_POLICY_H

#include "bongo_cat/model.h"
#include <math.h>

/* Shared by the native window and Cubism. Margins are canvas fractions, not
   framebuffer fractions. Growth in eighths amortizes surface reallocations. */
static inline bool bongo_cat_frame_valid(BongoCatLive2DFrame f) {
    return isfinite(f.left) && isfinite(f.top) && isfinite(f.right) &&
        isfinite(f.bottom) && f.left >= 0 && f.top >= 0 &&
        f.right >= 0 && f.bottom >= 0;
}

static inline bool bongo_cat_frame_equal(BongoCatLive2DFrame a,
    BongoCatLive2DFrame b) {
    return a.left == b.left && a.top == b.top &&
        a.right == b.right && a.bottom == b.bottom;
}

static inline float bongo_cat_frame_margin(float previous, double overflow) {
    if (!isfinite(overflow) || overflow <= previous) return previous;
    /* Half a bucket of headroom, rounded outwards. No shrinking until unload. */
    return (float)(ceil((overflow + 0.0625) * 8.0) / 8.0);
}

static inline BongoCatLive2DFrame bongo_cat_frame_observe(
    BongoCatLive2DFrame previous, float min_x, float min_y,
    float max_x, float max_y) {
    if (!isfinite(min_x) || !isfinite(min_y) || !isfinite(max_x) ||
        !isfinite(max_y) || min_x > max_x || min_y > max_y) return previous;
    previous.left = bongo_cat_frame_margin(previous.left, (-1.0 - min_x) * 0.5);
    previous.right = bongo_cat_frame_margin(previous.right, (max_x - 1.0) * 0.5);
    previous.top = bongo_cat_frame_margin(previous.top, (max_y - 1.0) * 0.5);
    previous.bottom = bongo_cat_frame_margin(previous.bottom, (-1.0 - min_y) * 0.5);
    return previous;
}

static inline double bongo_cat_frame_area(BongoCatLive2DFrame f) {
    return (1.0 + f.left + f.right) * (1.0 + f.top + f.bottom);
}

static inline BongoCatLive2DFrame bongo_cat_frame_mix(
    BongoCatLive2DFrame a, BongoCatLive2DFrame b, double t) {
    BongoCatLive2DFrame f;
    f.left = (float)(a.left + fmax(0.0, b.left - a.left) * t);
    f.top = (float)(a.top + fmax(0.0, b.top - a.top) * t);
    f.right = (float)(a.right + fmax(0.0, b.right - a.right) * t);
    f.bottom = (float)(a.bottom + fmax(0.0, b.bottom - a.bottom) * t);
    return f;
}

/* Budgets are relative to the content canvas. Preserve an existing allocation
   if a DPI/display change makes it exceed a budget; never enlarge it further. */
static inline BongoCatLive2DFrame bongo_cat_frame_limit(
    BongoCatLive2DFrame current, BongoCatLive2DFrame required,
    double area_limit, double width_limit, double height_limit) {
    if (!bongo_cat_frame_valid(current) || !bongo_cat_frame_valid(required) ||
        !isfinite(area_limit) || !isfinite(width_limit) || !isfinite(height_limit))
        return current;
    area_limit = fmax(area_limit, bongo_cat_frame_area(current));
    width_limit = fmax(width_limit, 1.0 + current.left + current.right);
    height_limit = fmax(height_limit, 1.0 + current.top + current.bottom);
    double low = 0.0, high = 1.0;
    for (int i = 0; i < 25; ++i) {
        double t = i == 0 ? 1.0 : (low + high) * 0.5;
        BongoCatLive2DFrame f = bongo_cat_frame_mix(current, required, t);
        if (bongo_cat_frame_area(f) <= area_limit &&
            1.0 + f.left + f.right <= width_limit &&
            1.0 + f.top + f.bottom <= height_limit) {
            low = t;
            if (t == 1.0) break;
        } else high = t;
    }
    BongoCatLive2DFrame f = bongo_cat_frame_mix(current, required, low);
    /* Round down whole buckets to keep a hard budget and avoid tiny repeated
       resizes as an extreme motion pushes against the allocation limit. */
    f.left = current.left + (float)(floor((f.left - current.left) * 8.0) / 8.0);
    f.top = current.top + (float)(floor((f.top - current.top) * 8.0) / 8.0);
    f.right = current.right + (float)(floor((f.right - current.right) * 8.0) / 8.0);
    f.bottom = current.bottom + (float)(floor((f.bottom - current.bottom) * 8.0) / 8.0);
    /* Spend the rounding remainder now. Otherwise the same unchanged request
       can creep outward over several frames, reallocating a surface each time. */
    float *edges[] = {&f.left, &f.right, &f.top, &f.bottom};
    const float targets[] = {required.left, required.right, required.top, required.bottom};
    for (int i = 0; i < 4; ++i) {
        double horizontal = 1.0 + f.left + f.right;
        double vertical = 1.0 + f.top + f.bottom;
        double room = i < 2 ? fmin(width_limit, area_limit / vertical) - horizontal :
            fmin(height_limit, area_limit / horizontal) - vertical;
        double extra = floor(fmax(0.0, fmin(room, targets[i] - *edges[i])) * 8.0) / 8.0;
        *edges[i] += (float)extra;
    }
    return f;
}

typedef struct BongoCatFrameViewport {
    int x, y, width, height;
    float scale;
} BongoCatFrameViewport;

static inline BongoCatFrameViewport bongo_cat_frame_viewport(
    BongoCatLive2DFrame allocated, BongoCatLive2DFrame required,
    int width, int height, bool flip) {
    BongoCatFrameViewport v = {0, 0, width, height, 1.0f};
    if (width <= 0 || height <= 0 || !bongo_cat_frame_valid(allocated) ||
        !bongo_cat_frame_valid(required)) return v;
    required = bongo_cat_frame_mix(allocated, required, 1.0);
    double horizontal = 1.0 + allocated.left + allocated.right;
    double vertical = 1.0 + allocated.top + allocated.bottom;
    double scale = fmin(horizontal / (1.0 + required.left + required.right),
        vertical / (1.0 + required.top + required.bottom));
    double cw = width / horizontal * scale, ch = height / vertical * scale;
    double bottom = flip ? required.top : required.bottom;
    double top = flip ? required.bottom : required.top;
    v.width = (int)fmax(1.0, scale >= 1.0 ? round(cw) : floor(cw));
    v.height = (int)fmax(1.0, scale >= 1.0 ? round(ch) : floor(ch));
    v.x = (int)lround(fmax(0.0, fmin(width - v.width,
        (width - v.width * (1.0 + required.right - required.left)) * 0.5)));
    v.y = (int)lround(fmax(0.0, fmin(height - v.height,
        (height - v.height * (1.0 + top - bottom)) * 0.5)));
    v.scale = (float)scale;
    return v;
}

#endif
