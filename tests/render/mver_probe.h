#ifndef BONGO_CAT_MVER_PROBE_H
#define BONGO_CAT_MVER_PROBE_H
#include "mver_pointer_overlay_internal.h"
#include <stddef.h>
#include <stdint.h>

/* Test-only GL sink: preserves every call and argument in sequence, including
   the complete uploaded vertex bytes. No OpenGL context is needed. */
typedef struct MverTrace {
    unsigned char bytes[65536];
    size_t size;
    unsigned draws;
} MverTrace;
extern MverTrace mver_trace;
extern bool mver_trace_enabled;
extern volatile uint64_t mver_probe_sink;
void mver_probe_reset(void);
void mver_probe_consume_geometry(const BongoCatMverPointerGeometry *value);
BongoCatMverPointerOverlay mver_probe_overlay(void);
void mver_reference_before(BongoCatMverPointerOverlay *value);
void mver_reference_after(BongoCatMverPointerOverlay *value);
void mver_probe_disable(GLenum cap);
void mver_probe_enable(GLenum cap);
void mver_probe_bind_texture(GLenum target, GLuint texture);
void mver_probe_draw_arrays(GLenum mode, GLint first, GLsizei count);
void mver_probe_delete_textures(GLsizei count, const GLuint *textures);
/* Separate correctness-only subjects; never called by timed benchmark paths. */
extern unsigned mver_geometry_calls, mver_reference_geometry_calls;
bool mver_counted_reference_geometry(float x, float y,
    const BongoCatMverPointerConfig *config, BongoCatMverPointerGeometry *output);
bool mver_counted_overlay_geometry(BongoCatMverPointerOverlay *value,
    BongoCatMverPointerGeometry *output);
void mver_counted_before(BongoCatMverPointerOverlay *value);
void mver_counted_after(BongoCatMverPointerOverlay *value);
void mver_counted_reference_before(BongoCatMverPointerOverlay *value);
void mver_counted_reference_after(BongoCatMverPointerOverlay *value);
#endif
