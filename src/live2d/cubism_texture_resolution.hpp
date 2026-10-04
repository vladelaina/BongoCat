#ifndef BONGO_CAT_CUBISM_TEXTURE_RESOLUTION_HPP
#define BONGO_CAT_CUBISM_TEXTURE_RESOLUTION_HPP

#include <utility>

namespace bongo_cat {

struct TextureResolution {
    int max_width = 0;
    int max_height = 0;
    bool resized = false;
};

/* Pick an atlas size from the physical content display/reference ratio at
   1x, rounding bounds upward. The atlas is a parts sheet, not a window-sized
   portrait. Quality additionally caps the area relative to the original atlas;
   it does not multiply an already reduced display budget. */
TextureResolution texture_resolution_for(bool enabled,
    int display_width, int display_height, int reference_width,
    int reference_height, int source_width, int source_height,
    int texture_limit, float quality_percent = 100.0f);

bool texture_quality_valid(float quality_percent);
/* Actual decoder dimensions, including integer aspect fitting. */
std::pair<int, int> texture_fitted_size(int source_width, int source_height,
    const TextureResolution &bound);

} // namespace bongo_cat

#endif
