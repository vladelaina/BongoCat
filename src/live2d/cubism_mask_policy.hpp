#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace bongo_cat {

struct MaskSize { int width = 0, height = 0; };

inline int mask_buffer_count(size_t count) {
    // Cubism packs 36 contexts into one target, or 32 into each of several.
    return count <= 36 ? 1 : (int)(1 + (count - 1) / 32);
}

inline MaskSize mask_layout(size_t count, int buffers) {
    if (!count) return {};
    size_t per_buffer = (count + (size_t)buffers - 1) / (size_t)buffers;
    size_t per_channel = (per_buffer + 3) / 4;
    if (per_channel <= 1) return {1, 1};
    if (per_channel == 2) return {2, 1};
    if (per_channel <= 4) return {2, 2};
    return {3, 3};
}

inline MaskSize mask_target_size(MaskSize layout, int width, int height,
    int limit, MaskSize previous = {}) {
    if (!layout.width || !layout.height || width <= 0 || height <= 0 || limit <= 0)
        return {};
    auto dimension = [limit](int pixels, int divisions, int old) {
        // SDK clipping adds 5% on both sides. Budget one mask texel per output
        // pixel for a context spanning the viewport, before hardware clamping.
        int64_t requested = ((int64_t)pixels * 11 * divisions + 9) / 10;
        int64_t rounded = std::max<int64_t>(256, ((requested + 127) / 128) * 128);
        int result = (int)std::min<int64_t>(limit, rounded);
        // Grow immediately; shrink only after crossing two allocation buckets.
        if (result < old && old <= limit && requested > old - 256) return old;
        return result;
    };
    return {dimension(width, layout.width, previous.width),
        dimension(height, layout.height, previous.height)};
}

inline bool operator==(MaskSize left, MaskSize right) {
    return left.width == right.width && left.height == right.height;
}

} // namespace bongo_cat
