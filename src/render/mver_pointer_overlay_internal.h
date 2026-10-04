#ifndef BONGO_CAT_MVER_POINTER_OVERLAY_INTERNAL_H
#define BONGO_CAT_MVER_POINTER_OVERLAY_INTERNAL_H

#include "mver_pointer_overlay.h"
#include "bongo_cat/gl_api.h"
#include "bongo_cat/mver_pointer.h"

typedef struct BongoCatPointerTexture {
    unsigned int id;
    int width;
    int height;
} BongoCatPointerTexture;

struct BongoCatMverPointerOverlay {
    BongoCatGL gl;
    unsigned int program;
    int textured_location;
    int image_location;
    unsigned int vao;
    unsigned int vbo;
    BongoCatPointerTexture arm;
    BongoCatPointerTexture device;
    BongoCatPointerTexture left;
    BongoCatPointerTexture right;
    BongoCatPointerTexture side;
    BongoCatMverPointerConfig geometry;
    int reference_width;
    int reference_height;
    float scale;
    float x_ratio;
    float y_ratio;
    float line_red;
    float line_green;
    float line_blue;
    bool enabled;
    bool vertical_flip;
    bool mouse;
    bool left_handed;
    bool left_down;
    bool right_down;
    bool side_down;
};

#endif
