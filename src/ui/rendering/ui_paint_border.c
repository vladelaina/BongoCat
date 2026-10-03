#include "ui_paint_border.h"

#include <math.h>
#include <stddef.h>

static float clamp01(float value) {
    return NK_CLAMP(0.0f, value, 1.0f);
}

static void corner(float x, float y, float cx, float cy, float radius,
    float base, float start, float *distance, float *along) {
    float angle = atan2f(y - cy, x - cx);
    if (angle < start) angle += 6.28318530718f;
    *distance = fabsf(hypotf(x - cx, y - cy) - radius);
    *along = base + (angle - start) * radius;
}

static void sample(float x, float y, float width, float height, float radius,
    float thickness, float *distance, float *along) {
    float inset = thickness * .5f;
    float left = inset, top = inset, right = width - inset;
    float bottom = height - inset;
    radius = NK_CLAMP(1.0f, radius - inset,
        NK_MIN(right - left, bottom - top) * .5f);
    float lx = left + radius, rx = right - radius;
    float ty = top + radius, by = bottom - radius;
    float horizontal = NK_MAX(0.0f, rx - lx);
    float vertical = NK_MAX(0.0f, by - ty);
    float arc = 1.57079632679f * radius;
    if (x < lx && y < ty) {
        corner(x, y, lx, ty, radius,
            horizontal * 2 + vertical * 2 + arc * 3,
            3.14159265359f, distance, along); return;
    }
    if (x > rx && y < ty) {
        corner(x, y, rx, ty, radius, horizontal,
            -1.57079632679f, distance, along); return;
    }
    if (x > rx && y > by) {
        corner(x, y, rx, by, radius, horizontal + arc + vertical,
            0, distance, along); return;
    }
    if (x < lx && y > by) {
        corner(x, y, lx, by, radius,
            horizontal * 2 + vertical + arc * 2,
            1.57079632679f, distance, along); return;
    }
    float choices[4] = {fabsf(y - top), fabsf(x - right),
        fabsf(y - bottom), fabsf(x - left)};
    int nearest = 0;
    for (int i = 1; i < 4; ++i)
        if (choices[i] < choices[nearest]) nearest = i;
    *distance = choices[nearest];
    if (nearest == 0) *along = NK_CLAMP(lx, x, rx) - lx;
    else if (nearest == 1) *along = horizontal + arc + NK_CLAMP(ty, y, by) - ty;
    else if (nearest == 2) *along = horizontal + arc + vertical + arc +
        rx - NK_CLAMP(lx, x, rx);
    else *along = horizontal * 2 + vertical + arc * 3 +
        by - NK_CLAMP(ty, y, by);
}

void bongo_cat_ui_raster_dashed_rounded(unsigned char *pixels,
    int width, int height, float radius, float thickness, float dash,
    float gap, struct nk_color color) {
    float period = NK_MAX(1.0f, dash + gap);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        float distance, along;
        sample(x + .5f, y + .5f, (float)width, (float)height, radius,
            thickness, &distance, &along);
        float phase = fmodf(along + dash * .5f, period);
        float dash_alpha = phase <= dash ?
            clamp01(NK_MIN(phase + .5f, dash - phase + .5f)) : 0.0f;
        float alpha = clamp01(thickness * .5f + .75f - distance) * dash_alpha;
        unsigned char *target = pixels + ((size_t)y * width + x) * 4;
        unsigned char coverage = (unsigned char)(color.a * alpha + .5f);
        target[0] = coverage ? color.r : 0;
        target[1] = coverage ? color.g : 0;
        target[2] = coverage ? color.b : 0;
        target[3] = coverage;
    }
}
