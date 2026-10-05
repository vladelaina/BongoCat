/* Correctness-only copies. The timing subjects remain uninstrumented. */
#include "mver_probe.h"

unsigned mver_geometry_calls, mver_reference_geometry_calls;
bool mver_counted_geometry(float x, float y,
    const BongoCatMverPointerConfig *config, BongoCatMverPointerGeometry *output) {
    mver_geometry_calls++;
    return bongo_cat_mver_pointer_geometry(x, y, config, output);
}
bool mver_counted_reference_geometry(float x, float y,
    const BongoCatMverPointerConfig *config, BongoCatMverPointerGeometry *output) {
    mver_reference_geometry_calls++;
    return bongo_cat_mver_pointer_geometry(x, y, config, output);
}
#define bongo_cat_mver_pointer_geometry mver_counted_geometry
#define bongo_cat_mver_pointer_overlay_geometry mver_counted_overlay_geometry
#define bongo_cat_mver_pointer_overlay_draw_before_keys mver_counted_before
#define bongo_cat_mver_pointer_overlay_draw_after_keys mver_counted_after
#include "../../src/render/mver_pointer_overlay_geometry.c"
#include "mver_draw_subject.c"
