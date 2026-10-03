#ifndef BONGO_CAT_GL_READBACK_H
#define BONGO_CAT_GL_READBACK_H

#include <stdbool.h>

/* RGBA from the window framebuffer, independent of renderer FBO/PBO state. */
bool bongo_cat_gl_read_window(int x, int y, int width, int height,
    bool back_buffer, void *pixels);

#endif
