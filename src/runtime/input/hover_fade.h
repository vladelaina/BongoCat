#ifndef BONGO_CAT_HOVER_FADE_H
#define BONGO_CAT_HOVER_FADE_H

#include <stdbool.h>

static inline float bongo_cat_hover_fade_curve(float phase) {
    return phase * phase * (3.0f - 2.0f * phase);
}

/* Recover the curve position from the last successfully displayed opacity. */
static inline float bongo_cat_hover_fade_phase(float opacity) {
    if (opacity <= 0.0f) return 0.0f;
    if (opacity >= 1.0f) return 1.0f;
    float low = 0.0f, high = 1.0f;
    for (int i = 0; i < 20; ++i) {
        float mid = (low + high) * 0.5f;
        if (bongo_cat_hover_fade_curve(mid) < opacity) low = mid;
        else high = mid;
    }
    return (low + high) * 0.5f;
}

static inline float bongo_cat_hover_fade_step(float phase, bool hidden,
    float elapsed, float duration) {
    if (duration <= 0.0f) return hidden ? 0.0f : 1.0f;
    if (elapsed <= 0.0f) return phase;
    phase += (hidden ? -elapsed : elapsed) / duration;
    return phase < 0.0f ? 0.0f : phase > 1.0f ? 1.0f : phase;
}

#endif
