#ifndef BONGO_CAT_CUBISM_TEXTURE_RESOLUTION_HPP
#define BONGO_CAT_CUBISM_TEXTURE_RESOLUTION_HPP

namespace bongo_cat {

struct TextureResolution {
    int max_width = 0;
    int max_height = 0;
    bool resized = false;
};

/* Pick an atlas size from the physical content display/reference ratio at
   1x, rounding bounds upward. The atlas is a parts sheet, not a window-sized
   portrait. Resolution policy stays independent of loading and decoding. */
TextureResolution texture_resolution_for(bool enabled,
    int display_width, int display_height, int reference_width,
    int reference_height, int source_width, int source_height,
    int texture_limit);

} // namespace bongo_cat

#endif
