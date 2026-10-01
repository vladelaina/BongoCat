#include "cubism_core_profile.hpp"
#include "cubism_runtime.hpp"
#include <SDL3/SDL.h>
#include <cstdio>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Failed at line %d: %s (%s)\n", __LINE__, \
        #condition, SDL_GetError()); return 1; } } while (0)

static GLuint shader(GLenum type, const char *source) {
    GLuint result = glCreateShader(type);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    return result;
}

int main() {
    CHECK(SDL_Init(SDL_INIT_VIDEO));
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("Core profile test", 32, 32,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    CHECK(window);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    CHECK(context);
    glewExperimental = GL_TRUE;
    CHECK(glewInit() == GLEW_OK);
    while (glGetError() != GL_NO_ERROR) {}
    GLuint vertex = shader(GL_VERTEX_SHADER,
        "#version 330 core\nlayout(location=3) in vec2 p;"
        "layout(location=7) in vec2 uv; out vec2 v;"
        "void main(){gl_Position=vec4(p,0,1);v=uv;}");
    GLuint fragment = shader(GL_FRAGMENT_SHADER,
        "#version 330 core\nin vec2 v; out vec4 color;"
        "void main(){color=vec4(v,0,1);}");
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    CHECK(linked);
    glUseProgram(program);
    glViewport(0, 0, 32, 32);
    const GLfloat triangle[] = {-1,-1, 3,-1, -1,3};
    const GLfloat quad[] = {-1,-1, 1,-1, 1,1, -1,1};
    const GLushort triangle_indices[] = {0,1,2};
    const GLushort quad_indices[] = {0,1,2, 2,3,0};
    const GLfloat triangle_uv[] = {1,0, 1,0, 1,0};
    const GLfloat quad_uv[] = {1,0, 1,0, 1,0, 1,0};
    GLuint host_vao, host_buffers[2];
    glGenVertexArrays(1, &host_vao);
    glGenBuffers(2, host_buffers);
    glBindVertexArray(host_vao);
    glBindBuffer(GL_ARRAY_BUFFER, host_buffers[0]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, host_buffers[1]);
    for (int iteration = 0; iteration < 32; ++iteration) {
        bongo_cat::CoreProfileBuffers outer, inner;
        GLuint retained_vao = 0, retained_buffer = 0;
        for (int frame = 0; frame < 8; ++frame) {
            bongo_cat::CoreProfileBinding binding(outer);
            if (frame) {
                CHECK(outer.vao == retained_vao);
                CHECK(outer.buffers[0] == retained_buffer);
            }
            retained_vao = outer.vao;
            retained_buffer = outer.buffers[0];
            glEnableVertexAttribArray(3);
            glEnableVertexAttribArray(7);
            bongo_cat::CoreProfileBinding::attribute(0, 3, triangle, sizeof(triangle));
            bongo_cat::CoreProfileBinding::attribute(1, 7, triangle_uv, sizeof(triangle_uv));
            bongo_cat::CoreProfileBinding::draw(3, triangle_indices);
            {
                bongo_cat::CoreProfileBinding nested(inner);
                glEnableVertexAttribArray(3);
                glEnableVertexAttribArray(7);
                bongo_cat::CoreProfileBinding::attribute(0, 3, quad, sizeof(quad));
                bongo_cat::CoreProfileBinding::attribute(1, 7, quad_uv, sizeof(quad_uv));
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                // Cubism PreDraw clears the element binding before some passes.
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
                bongo_cat::CoreProfileBinding::draw(6, quad_indices);
                unsigned char pixel[4] = {};
                glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                CHECK(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0);
            }
            GLint current = 0;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current);
            CHECK((GLuint)current == outer.vao);
            bongo_cat::CoreProfileBinding::draw(3, triangle_indices);
            unsigned char pixel[4] = {};
            glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            CHECK(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0);
        }
        GLint current = -1;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &current);
        CHECK((GLuint)current == host_vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &current);
        CHECK((GLuint)current == host_buffers[0]);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &current);
        CHECK((GLuint)current == host_buffers[1]);
        GLuint vao = outer.vao;
        GLuint buffers[] = {outer.buffers[0], outer.buffers[1], outer.buffers[2],
            inner.buffers[0], inner.buffers[1], inner.buffers[2]};
        for (GLuint buffer : buffers) CHECK(glIsBuffer(buffer));
        outer.release();
        inner.release();
        CHECK(!glIsVertexArray(vao));
        for (GLuint buffer : buffers) CHECK(!glIsBuffer(buffer));
        outer.release();
        CHECK(outer.vao == 0 && outer.buffers[0] == 0);
        CHECK(glGetError() == GL_NO_ERROR);
    }
    BongoCatLive2D runtime{};
    runtime.retired_count = 1;
    runtime.retired[0].frames_remaining = 3;
    for (int frame = 0; frame < 3; ++frame) {
        CHECK(bongo_cat_live2d_update(&runtime, 1.0f / 60.0f));
        bongo_cat_live2d_draw(&runtime);
    }
    CHECK(runtime.retired_count == 0);
    CHECK(!bongo_cat_live2d_update(&runtime, 1.0f / 60.0f));
    glDeleteVertexArrays(1, &host_vao);
    glDeleteBuffers(2, host_buffers);
    glDeleteProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
