#include "cubism_texture_resolution.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bongo_cat {

namespace {
/* Match the physical content display/reference ratio without extra linear
   headroom. Atlas dimensions still follow the model's part layout. */
constexpr double kDisplayScale = 1.0;
constexpr int kDimensionStep = 64;

int rounded_dimension(double value, int source, int limit) {
    if (!std::isfinite(value) || value <= 0.0) return 1;
    int64_t rounded = (int64_t)std::ceil(value);
    if constexpr (kDimensionStep > 1)
        rounded = ((rounded + kDimensionStep - 1) / kDimensionStep) *
            kDimensionStep;
    rounded = std::max<int64_t>(1, rounded);
    rounded = std::min<int64_t>(rounded, source);
    if (limit > 0) rounded = std::min<int64_t>(rounded, limit);
    return (int)rounded;
}
} // namespace

TextureResolution texture_resolution_for(bool enabled,
    int display_width, int display_height, int reference_width,
    int reference_height, int source_width, int source_height,
    int texture_limit) {
    TextureResolution result{source_width, source_height, false};
    if (!enabled || source_width <= 0 || source_height <= 0) return result;
    if (display_width <= 0 || display_height <= 0 || reference_width <= 0 ||
        reference_height <= 0) return result;
    double scale_x = (double)display_width / reference_width;
    double scale_y = (double)display_height / reference_height;
    double display_scale = std::max(scale_x, scale_y);
    if (!std::isfinite(display_scale) || display_scale <= 0.0) return result;
    double scale = std::min(1.0, display_scale * kDisplayScale);
    int max_width = rounded_dimension(source_width * scale,
        source_width, texture_limit);
    int max_height = rounded_dimension(source_height * scale,
        source_height, texture_limit);
    result.max_width = max_width;
    result.max_height = max_height;
    result.resized = max_width < source_width || max_height < source_height;
    return result;
}

} // namespace bongo_cat
