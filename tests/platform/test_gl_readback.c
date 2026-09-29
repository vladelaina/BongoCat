#include "test.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <string.h>

int bongo_cat_test_failures;
static GLint framebuffer = 17, pack_buffer = 23, alignment = 8;
static GLint row_length = 99, skip_pixels = 7, skip_rows = 9;
static GLint window_buffer = GL_FRONT, offscreen_buffer = GL_COLOR_ATTACHMENT0;
static GLenum expected_buffer, read_error;
static unsigned reads;
static void get(GLenum name, GLint *value) {
    switch (name) {
    case GL_READ_FRAMEBUFFER_BINDING: *value = framebuffer; break;
    case GL_PIXEL_PACK_BUFFER_BINDING: *value = pack_buffer; break;
    case GL_PACK_ALIGNMENT: *value = alignment; break;
    case GL_PACK_ROW_LENGTH: *value = row_length; break;
    case GL_PACK_SKIP_PIXELS: *value = skip_pixels; break;
    case GL_PACK_SKIP_ROWS: *value = skip_rows; break;
    case GL_READ_BUFFER: *value = framebuffer ? offscreen_buffer : window_buffer; break;
    default: CHECK(false); *value = 0;
    }
}
static void APIENTRY bind_buffer(GLenum target, GLuint value) {
    CHECK(target == GL_PIXEL_PACK_BUFFER); pack_buffer = (GLint)value;
}
static void APIENTRY bind_framebuffer(GLenum target, GLuint value) {
    CHECK(target == GL_READ_FRAMEBUFFER); framebuffer = (GLint)value;
}
static SDL_FunctionPointer proc(const char *name) {
    if (!strcmp(name, "glBindBuffer")) return (SDL_FunctionPointer)bind_buffer;
    if (!strcmp(name, "glBindFramebuffer")) return (SDL_FunctionPointer)bind_framebuffer;
    CHECK(false); return NULL;
}
static void read_buffer(GLenum value) {
    if (framebuffer) offscreen_buffer = (GLint)value;
    else window_buffer = (GLint)value;
}
static void store(GLenum name, GLint value) {
    switch (name) {
    case GL_PACK_ALIGNMENT: alignment = value; break;
    case GL_PACK_ROW_LENGTH: row_length = value; break;
    case GL_PACK_SKIP_PIXELS: skip_pixels = value; break;
    case GL_PACK_SKIP_ROWS: skip_rows = value; break;
    default: CHECK(false);
    }
}
static void fake_read(GLint x, GLint y, GLsizei width, GLsizei height,
    GLenum format, GLenum type, void *pixels) {
    ++reads;
    CHECK(x == 3 && y == 5 && width == 1 && height == 1);
    CHECK(format == GL_RGBA && type == GL_UNSIGNED_BYTE);
    CHECK(!framebuffer && !pack_buffer && (GLenum)window_buffer == expected_buffer);
    CHECK(alignment == 1 && !row_length && !skip_pixels && !skip_rows);
    memset(pixels, 0, 4); /* An empty pixel is a valid result, not a read failure. */
}
static GLenum error(void) { return read_error; }
#define SDL_GL_GetProcAddress proc
#define glGetIntegerv get
#define glReadBuffer read_buffer
#define glPixelStorei store
#define glReadPixels fake_read
#define glGetError error
#include "../../src/platform/common/gl_readback.c"

int main(void) {
    unsigned char guard[6] = {0xa5, 255, 255, 255, 255, 0xa5};
    for (int back = 0; back < 2; ++back) {
        expected_buffer = back ? GL_BACK : GL_FRONT;
        for (int fail = 0; fail < 2; ++fail) {
            read_error = fail ? GL_INVALID_OPERATION : GL_NO_ERROR;
            CHECK(bongo_cat_gl_read_window(3, 5, 1, 1, back != 0, guard + 1) == !fail);
            CHECK(guard[0] == 0xa5 && guard[5] == 0xa5 && guard[4] == 0);
            CHECK(framebuffer == 17 && pack_buffer == 23);
            CHECK(alignment == 8 && row_length == 99 && skip_pixels == 7 && skip_rows == 9);
            CHECK(window_buffer == GL_FRONT && offscreen_buffer == GL_COLOR_ATTACHMENT0);
        }
    }
    CHECK(reads == 4);
    CHECK(!bongo_cat_gl_read_window(-1, 0, 1, 1, false, guard + 1));
    CHECK(reads == 4);
    return bongo_cat_test_failures ? 1 : 0;
}
