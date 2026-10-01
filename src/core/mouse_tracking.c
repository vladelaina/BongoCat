#include "bongo_cat/mouse.h"

#include <math.h>

bool bongo_cat_mouse_sample_fresh(uint64_t now_ns, uint64_t sample_ns) {
    return sample_ns && now_ns >= sample_ns &&
        now_ns - sample_ns < 100000000ull;
}

void bongo_cat_mouse_target(BongoCatMouseTracking *tracking, double x, double y) {
    if (!tracking) return;
    tracking->target_x = x;
    tracking->target_y = y;
    if (!tracking->initialized) {
        tracking->current_x = x;
        tracking->current_y = y;
        tracking->initialized = true;
    }
    tracking->settled = false;
}

bool bongo_cat_mouse_step(BongoCatMouseTracking *tracking, float delta_seconds,
    double *x, double *y) {
    if (!tracking || !tracking->initialized || tracking->settled || !x || !y)
        return false;
    if (delta_seconds < 0.0f) delta_seconds = 0.0f;
    double alpha = 1.0 - pow(0.75, (double)delta_seconds * 60.0);
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;
    tracking->current_x += (tracking->target_x - tracking->current_x) * alpha;
    tracking->current_y += (tracking->target_y - tracking->current_y) * alpha;
    double dx = tracking->target_x - tracking->current_x;
    double dy = tracking->target_y - tracking->current_y;
    if (dx * dx + dy * dy < 0.25) {
        tracking->current_x = tracking->target_x;
        tracking->current_y = tracking->target_y;
        tracking->settled = true;
    }
    *x = tracking->current_x;
    *y = tracking->current_y;
    return true;
}

float bongo_cat_mouse_centered_ratio(double position, double center,
    double minimum, double maximum) {
    if (!isfinite(position) || !isfinite(center) || !isfinite(minimum) ||
        !isfinite(maximum) || maximum <= minimum) return 0.5f;
    if (center < minimum) center = minimum;
    if (center > maximum) center = maximum;
    if (position < center) {
        double span = center - minimum;
        if (span <= 0.0) return 0.5f;
        double ratio = 0.5 * (position - minimum) / span;
        return (float)(ratio < 0.0 ? 0.0 : ratio);
    }
    if (position > center) {
        double span = maximum - center;
        if (span <= 0.0) return 0.5f;
        double ratio = 0.5 + 0.5 * (position - center) / span;
        return (float)(ratio > 1.0 ? 1.0 : ratio);
    }
    return 0.5f;
}

static float clamp_pointer_ratio(float value) {
    if (!isfinite(value)) return 0.5f;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

void bongo_cat_mouse_drag_coordinates(float x_ratio, float y_ratio,
    bool horizontal_mirror, float *x, float *y) {
    float horizontal = clamp_pointer_ratio(x_ratio);
    float vertical = clamp_pointer_ratio(y_ratio);
    if (horizontal_mirror) horizontal = 1.0f - horizontal;
    if (x) *x = 2.0f * horizontal - 1.0f;
    if (y) *y = 1.0f - 2.0f * vertical;
}

static float parameter_from_normalized(float minimum, float maximum,
    float normalized) {
    if (!isfinite(minimum) || !isfinite(maximum) || maximum < minimum)
        return 0.0f;
    if (minimum <= 0.0f && maximum >= 0.0f)
        return normalized >= 0.0f ? normalized * maximum :
            -normalized * minimum;
    return minimum + (normalized + 1.0f) * 0.5f * (maximum - minimum);
}

float bongo_cat_mouse_parameter_value(float minimum, float maximum,
    float x_ratio, float y_ratio, char axis, bool mirror) {
    float x = 0.0f, y = 0.0f;
    bongo_cat_mouse_drag_coordinates(x_ratio, y_ratio, mirror, &x, &y);
    float normalized = axis == 'Y' ? y : axis == 'Z' ? -x * y : x;
    return parameter_from_normalized(minimum, maximum, normalized);
}
