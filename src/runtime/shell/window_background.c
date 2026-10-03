#include "runtime.h"
#include "bongo_cat/gl_api.h"

#include <SDL3/SDL_opengl.h>

static BongoCatGL corner_gl;
static GLuint corner_program, corner_vao;
static GLint corner_width, corner_height, corner_radius;
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
        "void main() {\n"
        " vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
        " gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";
    const char *fragment =
        "#version 330 core\n"
        "uniform int width, height, radiusMilli;\n"
        "out vec4 color;\n"
        "void main() {\n"
        " vec2 halfSize = vec2(width, height) * 0.5;\n"
        " float radius = float(radiusMilli) * 0.001;\n"
        " vec2 q = abs(gl_FragCoord.xy - halfSize) - (halfSize - radius);\n"
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
        width <= 0 || height <= 0 ||
        !prepare_corner_mask()) return;
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
    corner_gl.uniform_1i(corner_width, width);
    corner_gl.uniform_1i(corner_height, height);
    float percent = SDL_clamp(app->settings.window.corner_radius_percent, 0.0f, 50.0f);
    corner_gl.uniform_1i(corner_radius,
        (GLint)(SDL_min(width, height) * percent * 10.0f + 0.5f));
    glDrawArrays(GL_TRIANGLES, 0, 3);
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

void bongo_cat_window_clear_background(BongoCatApp *app) {
    BongoCatWindowPreferences *window = &app->settings.window;
    uint32_t rgb = bongo_cat_obs_background_color_rgb(
        window->obs_background_color);
    float alpha = window->obs_background ? 1.0f : 0.0f;
    glClearColor(window->obs_background ? ((rgb >> 16) & 255) / 255.0f : 0.0f,
        window->obs_background ? ((rgb >> 8) & 255) / 255.0f : 0.0f,
        window->obs_background ? (rgb & 255) / 255.0f : 0.0f, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
    static int last_enabled = -1;
    static BongoCatObsBackgroundColor last_color =
        BONGO_CAT_OBS_BACKGROUND_COLOR_COUNT;
    if (last_enabled != (window->obs_background ? 1 : 0) ||
        last_color != window->obs_background_color) {
        last_enabled = window->obs_background;
        last_color = window->obs_background_color;
        SDL_Log("OBS background mode: enabled=%d color=%s",
            window->obs_background,
            bongo_cat_obs_background_color_name(window->obs_background_color));
    }
}
