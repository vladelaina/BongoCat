#include "windows_gl_readback.h"
#include <SDL3/SDL_opengl.h>

bool bongo_cat_windows_gl_readback(int width, int height, void *pixels) {
    if (width <= 0 || height <= 0 || !pixels)
        return SDL_SetError("Invalid Windows frame readback dimensions");
    PFNGLBINDBUFFERPROC bind_buffer =
        (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    PFNGLBINDFRAMEBUFFERPROC bind_framebuffer =
        (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    if (!bind_buffer || !bind_framebuffer)
        return SDL_SetError("Windows frame readback requires OpenGL buffer bindings");
    GLint framebuffer, buffer, pack_buffer, alignment, row_length, skip_pixels, skip_rows;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_PACK_ROW_LENGTH, &row_length);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skip_pixels);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows);
    bind_framebuffer(GL_READ_FRAMEBUFFER, 0);
    /* GL_READ_BUFFER belongs to the bound framebuffer, so save/restore the
       default framebuffer's selector while that framebuffer is bound. */
    glGetIntegerv(GL_READ_BUFFER, &buffer);
    bind_buffer(GL_PIXEL_PACK_BUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    GLenum error = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, row_length);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skip_pixels);
    glPixelStorei(GL_PACK_SKIP_ROWS, skip_rows);
    glReadBuffer((GLenum)buffer);
    bind_buffer(GL_PIXEL_PACK_BUFFER, (GLuint)pack_buffer);
    bind_framebuffer(GL_READ_FRAMEBUFFER, (GLuint)framebuffer);
    return error == GL_NO_ERROR ||
        SDL_SetError("Windows frame readback failed: 0x%x", (unsigned)error);
}
