#ifndef BONGO_CAT_MOUSE_H
#define BONGO_CAT_MOUSE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct BongoCatMouseTracking {
    double target_x;
    double target_y;
    double current_x;
    double current_y;
    bool initialized;
    bool settled;
} BongoCatMouseTracking;

bool bongo_cat_mouse_sample_fresh(uint64_t now_ns, uint64_t sample_ns);
void bongo_cat_mouse_target(BongoCatMouseTracking *tracking, double x, double y);
bool bongo_cat_mouse_step(BongoCatMouseTracking *tracking, float delta_seconds,
    double *x, double *y);
float bongo_cat_mouse_centered_ratio(double position, double center,
    double minimum, double maximum);
void bongo_cat_mouse_drag_coordinates(float x_ratio, float y_ratio,
    bool horizontal_mirror, float *x, float *y);
float bongo_cat_mouse_parameter_value(float minimum, float maximum,
    float x_ratio, float y_ratio, char axis, bool mirror);

#endif
