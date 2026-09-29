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

bool texture_quality_valid(float quality_percent) {
    return quality_percent == 0.1f || quality_percent == 1.0f ||
        (quality_percent >= 10.0f && quality_percent <= 100.0f &&
            std::fmod(quality_percent, 10.0f) == 0.0f);
}

TextureResolution texture_resolution_for(bool enabled,
    int display_width, int display_height, int reference_width,
    int reference_height, int source_width, int source_height,
    int texture_limit, float quality_percent) {
    TextureResolution result{source_width, source_height, false};
    if (source_width <= 0 || source_height <= 0) return result;
    if (enabled && display_width > 0 && display_height > 0 &&
        reference_width > 0 && reference_height > 0) {
        double scale_x = (double)display_width / reference_width;
        double scale_y = (double)display_height / reference_height;
        double scale = std::min(1.0, std::max(scale_x, scale_y) * kDisplayScale);
        result.max_width = rounded_dimension(source_width * scale,
            source_width, texture_limit);
        result.max_height = rounded_dimension(source_height * scale,
            source_height, texture_limit);
    }
    if (texture_quality_valid(quality_percent) && quality_percent < 100.0f) {
        const double scale = std::sqrt((double)quality_percent / 100.0);
        result.max_width = std::min(result.max_width,
            std::max(1, (int)std::ceil(source_width * scale)));
        result.max_height = std::min(result.max_height,
            std::max(1, (int)std::ceil(source_height * scale)));
        if (texture_limit > 0) {
            result.max_width = std::min(result.max_width, texture_limit);
            result.max_height = std::min(result.max_height, texture_limit);
        }
    }
    result.resized = result.max_width < source_width ||
        result.max_height < source_height;
    return result;
}

std::pair<int, int> texture_fitted_size(int width, int height,
    const TextureResolution &bound) {
    if (width < 1 || height < 1 || bound.max_width < 1 || bound.max_height < 1)
        return {0, 0};
    if (width > bound.max_width || height > bound.max_height) {
        if ((int64_t)bound.max_width * height <= (int64_t)bound.max_height * width) {
            height = std::max(1, (int)((int64_t)height * bound.max_width / width));
            width = bound.max_width;
        } else {
            width = std::max(1, (int)((int64_t)width * bound.max_height / height));
            height = bound.max_height;
        }
    }
    return {width, height};
}

} // namespace bongo_cat
