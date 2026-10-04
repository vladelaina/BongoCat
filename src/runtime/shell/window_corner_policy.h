#ifndef BONGO_CAT_WINDOW_CORNER_POLICY_H
#define BONGO_CAT_WINDOW_CORNER_POLICY_H

#include <stdbool.h>
#include <math.h>

/* Pixel coordinates use OpenGL's bottom-left origin, like the model viewport.
   A corner mask only affects this rectangle; everything outside is preserved. */
typedef struct BongoCatCornerRect {
    int x, y, width, height, radius_milli;
} BongoCatCornerRect;

static inline BongoCatCornerRect bongo_cat_corner_rect(int width, int height,
    bool content, int x, int y, int cw, int ch, float percent) {
    BongoCatCornerRect rect = {0, 0, width, height, 0};
    if (width <= 0 || height <= 0) return rect;
    /* A stale/absent viewport during startup or resize falls back to the
       surface. Subtraction keeps validation safe for extreme dimensions. */
    if (content && x >= 0 && y >= 0 && cw > 0 && ch > 0 &&
        cw <= width && ch <= height && x <= width - cw && y <= height - ch) {
        rect.x = x;
        rect.y = y;
        rect.width = cw;
        rect.height = ch;
    }
    if (!isfinite(percent) || percent <= 0.0f) return rect;
    if (percent > 50.0f) percent = 50.0f;
    int edge = rect.width < rect.height ? rect.width : rect.height;
    rect.radius_milli = (int)(edge * (double)percent * 10.0 + 0.5);
    return rect;
}

#endif
