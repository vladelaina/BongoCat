#include "mver_probe.h"
#include "bongo_cat/image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MverTrace mver_trace;
bool mver_trace_enabled = true;
volatile uint64_t mver_probe_sink;
static void append(const void *value, size_t size) {
    if (!mver_trace_enabled) return;
    if (size > sizeof(mver_trace.bytes) - mver_trace.size) {
        fputs("Mver trace overflow\n", stderr);
        abort();
    }
    memcpy(mver_trace.bytes + mver_trace.size, value, size);
    mver_trace.size += size;
}
static void event(uint32_t kind, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const uint32_t values[] = {kind, a, b, c, d};
    append(values, sizeof(values));
    mver_probe_sink += kind + a + b + c + d;
}
void mver_probe_consume_geometry(const BongoCatMverPointerGeometry *value) {
    const unsigned char *bytes = (const unsigned char *)value;
    uint64_t sum = 0;
    for (size_t i = 0; i < sizeof(*value); i++) sum += bytes[i];
    mver_probe_sink += sum;
}
void mver_probe_reset(void) { mver_trace.size = mver_trace.draws = 0; }
void mver_probe_disable(GLenum cap) { event(1, cap, 0, 0, 0); }
void mver_probe_enable(GLenum cap) { event(2, cap, 0, 0, 0); }
void mver_probe_bind_texture(GLenum target, GLuint texture) {
    event(3, target, texture, 0, 0);
}
void mver_probe_draw_arrays(GLenum mode, GLint first, GLsizei count) {
    event(4, mode, (uint32_t)first, (uint32_t)count, 0);
    mver_trace.draws++;
}
void mver_probe_delete_textures(GLsizei count, const GLuint *textures) {
    event(5, (uint32_t)count, 0, 0, 0);
    append(textures, sizeof(*textures) * (size_t)count);
}
static void APIENTRY blend_equation(GLenum mode) { event(6, mode, 0, 0, 0); }
static void APIENTRY blend_func(GLenum a, GLenum b, GLenum c, GLenum d) {
    event(7, a, b, c, d);
}
static void APIENTRY use_program(GLuint value) { event(8, value, 0, 0, 0); }
static void APIENTRY uniform(GLint location, GLint value) {
    event(9, (uint32_t)location, (uint32_t)value, 0, 0);
}
static void APIENTRY active_texture(GLenum value) { event(10, value, 0, 0, 0); }
static void APIENTRY bind_vertex_array(GLuint value) { event(11, value, 0, 0, 0); }
static void APIENTRY bind_buffer(GLenum target, GLuint value) {
    event(12, target, value, 0, 0);
}
static void APIENTRY buffer_data(GLenum target, GLsizeiptr size,
    const void *data, GLenum usage) {
    event(13, target, (uint32_t)size, usage, 0);
    append(data, (size_t)size);
    /* Also retain a payload dependency in the no-copy timing mode. This file
       is deliberately compiled without IPO, keeping callbacks opaque. */
    if (size >= (GLsizeiptr)sizeof(uint32_t)) {
        uint32_t first;
        memcpy(&first, data, sizeof(first));
        mver_probe_sink += first;
    }
}
BongoCatMverPointerOverlay mver_probe_overlay(void) {
    BongoCatMverPointerOverlay value = {0};
    value.gl.blend_equation = blend_equation;
    value.gl.blend_func_separate = blend_func;
    value.gl.use_program = use_program;
    value.gl.uniform_1i = uniform;
    value.gl.active_texture = active_texture;
    value.gl.bind_vertex_array = bind_vertex_array;
    value.gl.bind_buffer = bind_buffer;
    value.gl.buffer_data = buffer_data;
    value.program = 7; value.vao = 11; value.vbo = 13;
    value.textured_location = 2; value.image_location = 3;
    value.arm = (BongoCatPointerTexture){17, 300, 220};
    value.device = (BongoCatPointerTexture){19, 94, 53};
    value.left = (BongoCatPointerTexture){23, 94, 53};
    value.right = (BongoCatPointerTexture){29, 94, 53};
    value.side = (BongoCatPointerTexture){31, 94, 53};
    value.enabled = true; value.mouse = true;
    value.reference_width = 1400; value.reference_height = 1400;
    value.scale = 1; value.x_ratio = value.y_ratio = .5f;
    value.line_red = .25f; value.line_green = .5f; value.line_blue = .75f;
    return value;
}
/* Lifecycle tests load genuine metadata and regular files, but isolate image
   decoding/GPU resource creation. IDs are deterministic across reloads. */
unsigned int bongo_cat_image_texture(const char *path, int *width, int *height,
    BongoCatError *error) {
    (void)error;
    *width = 94; *height = 53;
    return strstr(path, "arm.asset") ? 17 : 19;
}
bool bongo_cat_gl_load(BongoCatGL *gl, BongoCatError *error) {
    (void)gl; (void)error; return false;
}
unsigned int bongo_cat_gl_program(BongoCatGL *gl, const char *vertex,
    const char *fragment, BongoCatError *error) {
    (void)gl; (void)vertex; (void)fragment; (void)error; return 0;
}
