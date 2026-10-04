#ifndef BONGO_CAT_MOUSE_INTERNAL_H
#define BONGO_CAT_MOUSE_INTERNAL_H

#include "bongo_cat/app.h"

/* One update uses the same coordinate space for motion limits and model output.
   Resolve it from current display geometry; do not cache across window moves. */
typedef struct BongoCatMouseProjection {
    BongoCatMverPointerBounds bounds;
    double center_x, center_y;
    bool centered;
} BongoCatMouseProjection;

bool bongo_cat_app_mouse_projection(BongoCatApp *app, double screen_x,
    double screen_y, BongoCatMouseProjection *projection);
bool bongo_cat_app_map_pointer(BongoCatApp *app,
    const BongoCatMouseProjection *projection, bool relative_requested,
    double absolute_x, double absolute_y, double *x, double *y, bool *changed);
void bongo_cat_app_apply_mouse_coordinates(BongoCatApp *app,
    const BongoCatMouseProjection *projection, double hand_x, double hand_y,
    double gaze_x, double gaze_y);

#endif
