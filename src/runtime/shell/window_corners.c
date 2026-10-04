#include "runtime.h"
#include "window_corner_policy.h"
#include "bongo_cat/gl_api.h"

#include <SDL3/SDL_opengl.h>

static BongoCatGL corner_gl;
static GLuint corner_program, corner_vao;
static GLint corner_width, corner_height, corner_radius;
static GLint corner_x, corner_y, corner_surface_width, corner_surface_height;
static PFNGLBLENDEQUATIONSEPARATEPROC corner_equation_separate;
static bool corner_failed;

void bongo_cat_window_destroy_corner_mask(void) {
    if (corner_vao) corner_gl.delete_vertex_arrays(1, &corner_vao);
    if (corner_program) corner_gl.delete_program(corner_program);
    corner_vao = corner_program = 0;
    corner_failed = false;
}

static bool prepare_corner_mask(void) {
    if (corner_program) return true;
    if (corner_failed) return false;
    const char *vertex =
        "#version 330 core\n"
        "uniform int width, height, radiusMilli, originX, originY;\n"
        "uniform int surfaceWidth, surfaceHeight;\n"
        "void main() {\n"
        " const vec2 vertices[6] = vec2[6](vec2(0,0), vec2(1,0),\n"
        "   vec2(0,1), vec2(0,1), vec2(1,0), vec2(1,1));\n"
        " int corner = gl_VertexID / 6;\n"
        " vec2 side = vec2(corner & 1, (corner >> 1) & 1);\n"
        " vec2 size = vec2(width, height);\n"
        " vec2 span = min(vec2(float(radiusMilli) * 0.001 + 0.5), size * 0.5);\n"
        " vec2 p = vec2(originX, originY) + side * (size - span)\n"
        "   + vertices[gl_VertexID % 6] * span;\n"
        " gl_Position = vec4(p / vec2(surfaceWidth, surfaceHeight) * 2.0 - 1.0, 0, 1);\n"
        "}\n";
    const char *fragment =
        "#version 330 core\n"
        "uniform int width, height, radiusMilli, originX, originY;\n"
        "out vec4 color;\n"
        "void main() {\n"
        " vec2 halfSize = vec2(width, height) * 0.5;\n"
        " float radius = float(radiusMilli) * 0.001;\n"
        " vec2 q = abs(gl_FragCoord.xy - vec2(originX, originY) - halfSize)\n"
        "   - (halfSize - radius);\n"
        " float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;\n"
        " float coverage = clamp(0.5 - d, 0.0, 1.0);\n"
        " color = vec4(coverage);\n"
        "}\n";
    BongoCatError error = {0};
    corner_equation_separate = (PFNGLBLENDEQUATIONSEPARATEPROC)
        SDL_GL_GetProcAddress("glBlendEquationSeparate");
    if (corner_equation_separate && bongo_cat_gl_load(&corner_gl, &error))
        corner_program = bongo_cat_gl_program(&corner_gl, vertex, fragment, &error);
    if (corner_program) {
        corner_gl.gen_vertex_arrays(1, &corner_vao);
        if (corner_vao) {
            corner_width = corner_gl.uniform_location(corner_program, "width");
            corner_height = corner_gl.uniform_location(corner_program, "height");
            corner_radius = corner_gl.uniform_location(corner_program, "radiusMilli");
            corner_x = corner_gl.uniform_location(corner_program, "originX");
            corner_y = corner_gl.uniform_location(corner_program, "originY");
            corner_surface_width = corner_gl.uniform_location(corner_program, "surfaceWidth");
            corner_surface_height = corner_gl.uniform_location(corner_program, "surfaceHeight");
            return true;
        }
        bongo_cat_window_destroy_corner_mask();
    }
    corner_failed = true;
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Cannot initialize window corners: %s",
        error.message);
    return false;
}

void bongo_cat_window_mask_corners(BongoCatApp *app, int width, int height) {
    if (!app->settings.window.rounded_corners ||
        app->settings.window.corner_radius_percent <= 0.0f ||
        width <= 0 || height <= 0) return;
    int x = 0, y = 0, cw = 0, ch = 0;
    bool content = !app->settings.window.obs_background &&
        bongo_cat_live2d_viewport(app->live2d, &x, &y, &cw, &ch);
    BongoCatCornerRect rect = bongo_cat_corner_rect(width, height,
        content, x, y, cw, ch, app->settings.window.corner_radius_percent);
    if (rect.radius_milli <= 0 || !prepare_corner_mask()) return;
    GLint program, vao, equation_rgb, equation_alpha, src_rgb, dst_rgb,
        src_alpha, dst_alpha;
    GLboolean color_mask[4];
    const GLenum capabilities[] = {GL_BLEND, GL_DEPTH_TEST, GL_STENCIL_TEST,
        GL_SCISSOR_TEST, GL_CULL_FACE};
    GLboolean enabled[SDL_arraysize(capabilities)];
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equation_alpha);
    glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
    glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
    for (size_t i = 0; i < SDL_arraysize(capabilities); ++i) {
        enabled[i] = glIsEnabled(capabilities[i]);
        if (i == 0) glEnable(capabilities[i]);
        else glDisable(capabilities[i]);
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    corner_gl.blend_equation(GL_FUNC_ADD);
    /* Multiply premultiplied RGB and alpha together, including the solid
       capture background, before either native presentation path reads it. */
    glBlendFunc(GL_ZERO, GL_SRC_ALPHA);
    corner_gl.use_program(corner_program);
    corner_gl.bind_vertex_array(corner_vao);
    corner_gl.uniform_1i(corner_width, rect.width);
    corner_gl.uniform_1i(corner_height, rect.height);
    corner_gl.uniform_1i(corner_x, rect.x);
    corner_gl.uniform_1i(corner_y, rect.y);
    corner_gl.uniform_1i(corner_radius, rect.radius_milli);
    corner_gl.uniform_1i(corner_surface_width, width);
    corner_gl.uniform_1i(corner_surface_height, height);
    /* Four disjoint corner quads in one draw. No full-surface pass and no
       clipping of the extension margins. Half-pixel padding covers AA, while
       limiting spans to half-size prevents double blending at 100% rounding. */
    glDrawArrays(GL_TRIANGLES, 0, 24);
    corner_gl.bind_vertex_array((GLuint)vao);
    corner_gl.use_program((GLuint)program);
    corner_equation_separate(equation_rgb, equation_alpha);
    corner_gl.blend_func_separate(src_rgb, dst_rgb, src_alpha, dst_alpha);
    glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
    for (size_t i = 0; i < SDL_arraysize(capabilities); ++i) {
        if (enabled[i]) glEnable(capabilities[i]);
        else glDisable(capabilities[i]);
    }
}
