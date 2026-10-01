#include "image_internal.h"
#include "bongo_cat/gl_api.h"
#include "test.h"
#include <SDL3/SDL.h>
#include <stb_image_write.h>
#include <math.h>
#include <stdlib.h>

int bongo_cat_test_failures;
static bool straight_alpha;

static void check_sampling(GLuint texture) {
    BongoCatGL gl;
    BongoCatError error = {0};
    CHECK(bongo_cat_gl_load(&gl, &error));
    const char *vertex = "#version 330 core\n"
        "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "gl_Position=vec4(p*2.0-1.0,0,1);}";
    const char *fragment = "#version 330 core\n"
        "uniform sampler2D atlas; uniform int straightAlpha; out vec4 result;"
        "void main(){vec4 c=textureLod(atlas,vec2(0.25,0.5),0.0);"
        "if(straightAlpha!=0)c.rgb*=c.a;"
        "result=vec4(c.rgb+vec3(1.0-c.a),1.0);}";
    GLuint program = bongo_cat_gl_program(&gl, vertex, fragment, &error), vao = 0;
    CHECK(program != 0);
    if (!program) return;
    gl.gen_vertex_arrays(1, &vao);
    gl.bind_vertex_array(vao);
    gl.use_program(program);
    gl.uniform_1i(gl.uniform_location(program, "straightAlpha"), straight_alpha);
    gl.active_texture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glViewport(0, 0, 1, 1);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4] = {0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("  filtered white edge on white: %u %u %u (expected 255 255 255)\n",
        pixel[0], pixel[1], pixel[2]);
    CHECK(pixel[0] >= 254 && pixel[1] >= 254 && pixel[2] >= 254);
    CHECK(glGetError() == GL_NO_ERROR);
    gl.use_program(0);
    gl.bind_vertex_array(0);
    gl.delete_vertex_arrays(1, &vao);
    gl.delete_program(program);
}

static void fixture(const char *path, int width, int height, int kind) {
    unsigned char *pixels = malloc((size_t)width * height * 4);
    CHECK(pixels != NULL);
    if (!pixels) return;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        unsigned char *p = pixels + ((size_t)y * width + x) * 4;
        bool visible = kind == 3 ? (x % 2) == 0 : (x % 4) < 2;
        p[0] = p[1] = p[2] = visible ? 255 : 0;
        p[3] = visible ? (kind == 2 ? 64 : 255) : 0;
        if (!visible && (kind == 1 || kind == 3)) p[0] = 255;
    }
    CHECK(stbi_write_png(path, width, height, 4, pixels, width * 4));
    free(pixels);
}

static void check_levels(GLuint texture, int width, int height) {
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint filter = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
    CHECK(filter == GL_LINEAR_MIPMAP_LINEAR);
    int level = 0;
    do {
        unsigned char *pixels = malloc((size_t)width * height * 4);
        CHECK(pixels != NULL);
        if (!pixels) return;
        GLint actual_width = 0, actual_height = 0, format = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &actual_width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &actual_height);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_INTERNAL_FORMAT, &format);
        CHECK(actual_width == width && actual_height == height && format == GL_RGBA8);
        glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        CHECK(glGetError() == GL_NO_ERROR);
        int max_error = 0;
        for (int i = 0; i < width * height; ++i) {
            const unsigned char *p = pixels + i * 4;
            /* White at any coverage must remain white over a white desktop,
             * and contribute only its coverage over a black desktop. */
            for (int c = 0; c < 3; ++c)
                max_error = SDL_max(max_error, abs((int)p[c] - p[3]));
        }
        printf("  mip %d (%dx%d): maximum white-composite error = %d/255\n",
            level, width, height, max_error);
        CHECK(max_error <= 1);
        free(pixels);
        if (width == 1 && height == 1) break;
        width = SDL_max(1, width / 2);
        height = SDL_max(1, height / 2);
        ++level;
    } while (true);
}

static void model_case(const char *path, int source_width, int kind, bool direct) {
    fixture(path, source_width, 8, kind);
    BongoCatError error = {0};
    int width = 0, height = 0;
    BongoCatImageAlphaMask alpha;
    GLuint texture = bongo_cat_image_texture_model(path, direct,
        &width, &height, &alpha, NULL, NULL, &error);
    printf("case width=%d kind=%d direct=%d: %s\n", source_width, kind,
        direct, texture ? "loaded" : error.message);
    CHECK(texture != 0);
    if (texture) {
#ifdef _WIN32
        CHECK(width == SDL_min(source_width, 2048));
        CHECK(height == (source_width > 2048 ? 4 : 8));
#endif
        CHECK(alpha.width > 0 && alpha.height > 0);
        if (!straight_alpha) check_levels(texture, width, height);
        if (source_width == 8) check_sampling(texture);
        glDeleteTextures(1, &texture);
    }
    /* Ordinary image users retain straight-alpha pixels. */
    BongoCatImage original;
    CHECK(bongo_cat_image_load(path, &original, &error) == BONGO_CAT_OK);
    if (original.pixels) {
        CHECK(original.width == source_width);
        CHECK(original.pixels[0] == 255);
        CHECK(original.pixels[3] == (kind == 2 ? 64 : 255));
        bongo_cat_image_free(&original);
    }
}

int main(int argc, char **argv) {
    straight_alpha = argc > 1 && SDL_strcmp(argv[1], "--straight-alpha") == 0;
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("Image filter test", 32, 32,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    CHECK(window != NULL);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    CHECK(context != NULL);
    if (!context) { SDL_Quit(); return 1; }
    char *path = NULL;
    SDL_asprintf(&path, "%sbongocat-filter-%llu.png", SDL_GetBasePath(),
        (unsigned long long)SDL_GetTicksNS());
    CHECK(path != NULL);
    if (path) {
        for (int kind = 0; kind < 4; ++kind) {
            model_case(path, 8, kind, true);
            model_case(path, 8, kind, false);
#ifdef _WIN32
            model_case(path, 4096, kind, false);
            model_case(path, 4096, kind, true);
#endif
        }
        CHECK(SDL_RemovePath(path));
        SDL_free(path);
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
