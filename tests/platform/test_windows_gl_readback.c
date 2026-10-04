#include "test.h"
#include "windows_gl_readback.h"
#include <SDL3/SDL_opengl.h>
#include <string.h>

int bongo_cat_test_failures;
static GLint framebuffer, pack_buffer, alignment, row_length, skip_pixels, skip_rows;
static GLint window_buffer, offscreen_buffer;
static GLenum read_error;
static bool missing_proc;
static unsigned reads;

static void fake_get(GLenum name, GLint *value) {
    switch (name) {
    case GL_READ_FRAMEBUFFER_BINDING: *value = framebuffer; break;
    case GL_PIXEL_PACK_BUFFER_BINDING: *value = pack_buffer; break;
    case GL_PACK_ALIGNMENT: *value = alignment; break;
    case GL_PACK_ROW_LENGTH: *value = row_length; break;
    case GL_PACK_SKIP_PIXELS: *value = skip_pixels; break;
    case GL_PACK_SKIP_ROWS: *value = skip_rows; break;
    case GL_READ_BUFFER: *value = framebuffer ? offscreen_buffer : window_buffer; break;
    default: CHECK(false); *value = 0; break;
    }
}
static void APIENTRY fake_bind_buffer(GLenum target, GLuint value) {
    CHECK(target == GL_PIXEL_PACK_BUFFER);
    pack_buffer = (GLint)value;
}
static void APIENTRY fake_bind_framebuffer(GLenum target, GLuint value) {
    CHECK(target == GL_READ_FRAMEBUFFER);
    framebuffer = (GLint)value;
}
static SDL_FunctionPointer fake_proc(const char *name) {
    if (missing_proc) return NULL;
    if (strcmp(name, "glBindBuffer") == 0) return (SDL_FunctionPointer)fake_bind_buffer;
    if (strcmp(name, "glBindFramebuffer") == 0) return (SDL_FunctionPointer)fake_bind_framebuffer;
    CHECK(false);
    return NULL;
}
static void fake_read_buffer(GLenum value) {
    if (framebuffer) offscreen_buffer = (GLint)value;
    else window_buffer = (GLint)value;
}
static void fake_store(GLenum name, GLint value) {
    switch (name) {
    case GL_PACK_ALIGNMENT: alignment = value; break;
    case GL_PACK_ROW_LENGTH: row_length = value; break;
    case GL_PACK_SKIP_PIXELS: skip_pixels = value; break;
    case GL_PACK_SKIP_ROWS: skip_rows = value; break;
    default: CHECK(false); break;
    }
}
static void fake_read(GLint x, GLint y, GLsizei width, GLsizei height,
    GLenum format, GLenum type, void *pixels) {
    reads++;
    CHECK(x == 0 && y == 0 && width == 2 && height == 2);
    CHECK(format == GL_BGRA && type == GL_UNSIGNED_BYTE);
    CHECK(framebuffer == 0 && window_buffer == GL_BACK && pack_buffer == 0);
    CHECK(alignment == 1 && row_length == 0 && skip_pixels == 0 && skip_rows == 0);
    if (!read_error) memset(pixels, 0x7f, 16);
}
static GLenum fake_error(void) { return read_error; }

/* Exercise the production helper with an offscreen FBO and a pack PBO left
   bound by another renderer. No GL context or graphics hardware is needed. */
#define SDL_GL_GetProcAddress fake_proc
#define glGetIntegerv fake_get
#define glReadBuffer fake_read_buffer
#define glPixelStorei fake_store
#define glReadPixels fake_read
#define glGetError fake_error
#include "../../src/platform/windows/windows_gl_readback.c"

static void reset(void) {
    framebuffer = 17; pack_buffer = 23;
    alignment = 8; row_length = 99; skip_pixels = 7; skip_rows = 9;
    window_buffer = GL_FRONT; offscreen_buffer = GL_COLOR_ATTACHMENT0;
    read_error = GL_NO_ERROR; missing_proc = false; reads = 0;
}
static void check_restored(void) {
    CHECK(framebuffer == 17 && pack_buffer == 23);
    CHECK(alignment == 8 && row_length == 99 && skip_pixels == 7 && skip_rows == 9);
    CHECK(window_buffer == GL_FRONT && offscreen_buffer == GL_COLOR_ATTACHMENT0);
}
int main(void) {
    unsigned char guarded[18] = {0};
    guarded[0] = guarded[17] = 0xa5;
    reset();
    CHECK(bongo_cat_windows_gl_readback(2, 2, guarded + 1));
    CHECK(reads == 1 && guarded[1] == 0x7f && guarded[16] == 0x7f);
    CHECK(guarded[0] == 0xa5 && guarded[17] == 0xa5);
    check_restored();
    reset();
    read_error = GL_INVALID_OPERATION;
    CHECK(!bongo_cat_windows_gl_readback(2, 2, guarded + 1));
    CHECK(reads == 1);
    CHECK(strstr(SDL_GetError(), "readback failed") != NULL);
    check_restored();
    reset();
    missing_proc = true;
    CHECK(!bongo_cat_windows_gl_readback(2, 2, guarded + 1));
    CHECK(reads == 0);
    check_restored();
    reset();
    CHECK(!bongo_cat_windows_gl_readback(0, 2, guarded + 1));
    CHECK(!bongo_cat_windows_gl_readback(2, -1, guarded + 1));
    CHECK(!bongo_cat_windows_gl_readback(2, 2, NULL));
    CHECK(reads == 0);
    check_restored();
    return bongo_cat_test_failures ? 1 : 0;
}
