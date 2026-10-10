#include "mver_pointer_overlay_internal.h"

#include <string.h>

bool bongo_cat_mver_pointer_overlay_geometry(
    BongoCatMverPointerOverlay *value, BongoCatMverPointerGeometry *output) {
    if (!value || !output) return false;
    const float key[] = {value->x_ratio, value->y_ratio,
        value->geometry.offset_x, value->geometry.offset_y,
        value->geometry.hand_offset_x, value->geometry.hand_offset_y};
    /* Compare complete input representations, including signed zero. Recheck
       on both draw phases: the pointer anchor can change between them. */
    if (value->geometry_cache_valid &&
        value->geometry_cache_left_handed == value->left_handed &&
        memcmp(value->geometry_cache_key, key, sizeof(key)) == 0) {
        *output = value->cached_geometry;
        return true;
    }
    value->geometry_cache_valid = false;
    float x = value->left_handed ? 1.0f - value->x_ratio : value->x_ratio;
    if (!bongo_cat_mver_pointer_geometry(x, value->y_ratio,
        &value->geometry, output)) return false;
    value->cached_geometry = *output;
    memcpy(value->geometry_cache_key, key, sizeof(key));
    value->geometry_cache_left_handed = value->left_handed;
    value->geometry_cache_valid = true;
    return true;
}
