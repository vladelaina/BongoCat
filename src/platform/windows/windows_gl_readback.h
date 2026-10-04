#ifndef BONGO_CAT_WINDOWS_GL_READBACK_H
#define BONGO_CAT_WINDOWS_GL_READBACK_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Read tightly packed BGRA8 from the window back buffer, bottom row first.
   All framebuffer/read-buffer/pixel-pack bindings are restored on return. */
bool bongo_cat_windows_gl_readback(int width, int height, void *pixels);
#ifdef __cplusplus
}
#endif
#endif
