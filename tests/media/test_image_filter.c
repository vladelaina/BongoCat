#include "image_internal.h"
#include "bongo_cat/gl_api.h"
#include "test.h"
#include <SDL3/SDL.h>
#include <stb_image_write.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

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
        if (kind == 4) {
            p[0] = (unsigned char)((x * 17 + y * 13) % 256);
            p[1] = (unsigned char)((x * 31 + y * 7) % 256);
            p[2] = (unsigned char)((x * 11 + y * 23) % 256);
            p[3] = (x + y) % 3 == 0 ? 255 : ((x + y) % 3 == 1 ? 64 : 0);
        }
    }
    CHECK(stbi_write_png(path, width, height, 4, pixels, width * 4));
    free(pixels);
}

static void check_base_pixels(GLuint texture, const BongoCatImage *original) {
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint width = 0, height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    CHECK(width == original->width && height == original->height);
    if (width != original->width || height != original->height) return;
    size_t count = (size_t)width * height;
    unsigned char *pixels = malloc(count * 4);
    CHECK(pixels != NULL);
    if (!pixels) return;
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    CHECK(glGetError() == GL_NO_ERROR);
    size_t differences = 0;
    for (size_t i = 0; i < count; ++i) {
        const unsigned char *source = original->pixels + i * 4;
        const unsigned char *actual = pixels + i * 4;
        if (actual[3] != source[3]) ++differences;
        for (int c = 0; c < 3; ++c) {
            unsigned char expected = straight_alpha ? source[c] :
                (unsigned char)((source[c] * source[3] + 127) / 255);
            if (actual[c] != expected) ++differences;
        }
    }
    printf("  original texel differences: %llu\n", (unsigned long long)differences);
    CHECK(differences == 0);
    free(pixels);
}

static void check_levels(GLuint texture, int width, int height, bool white) {
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint filter = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
    CHECK(filter == GL_LINEAR_MIPMAP_LINEAR);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &filter);
    CHECK(filter == GL_LINEAR);
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
        if (actual_width != width || actual_height != height) { free(pixels); return; }
        glGetTexImage(GL_TEXTURE_2D, level, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        CHECK(glGetError() == GL_NO_ERROR);
        int max_error = 0;
        for (int i = 0; i < width * height; ++i) {
            const unsigned char *p = pixels + i * 4;
            /* White at any coverage must remain white over a white desktop,
             * and contribute only its coverage over a black desktop. */
            for (int c = 0; c < 3; ++c) {
                int error = white ? abs((int)p[c] - p[3]) : (int)p[c] - p[3];
                max_error = SDL_max(max_error, error);
            }
        }
        printf("  mip %d (%dx%d): maximum alpha-filter error = %d/255\n",
            level, width, height, max_error);
        CHECK(max_error <= 1);
        free(pixels);
        if (width == 1 && height == 1) break;
        width = SDL_max(1, width / 2);
        height = SDL_max(1, height / 2);
        ++level;
    } while (true);
}

typedef struct ProgressState { float last; int calls; } ProgressState;

static void check_progress(void *userdata, float progress) {
    ProgressState *state = userdata;
    CHECK(isfinite(progress) && progress >= state->last && progress <= 1.0f);
    state->last = progress;
    state->calls++;
}

static void model_case(const char *path, int source_width, int source_height,
    int kind, bool direct) {
    fixture(path, source_width, source_height, kind);
    BongoCatError error = {0};
    int width = 0, height = 0;
    BongoCatImageAlphaMask alpha;
    ProgressState progress = {0};
    GLuint texture = bongo_cat_image_texture_model(path, direct,
        &width, &height, &alpha, check_progress, &progress, &error);
    printf("case %dx%d kind=%d direct=%d: %s\n", source_width, source_height,
        kind, direct, texture ? "loaded" : error.message);
    CHECK(texture != 0);
    if (texture) {
        CHECK(width == source_width && height == source_height);
        CHECK(progress.calls > 0 && progress.last == 1.0f);
        CHECK(alpha.width > 0 && alpha.height > 0);
        if (!straight_alpha) check_levels(texture, width, height, kind < 4);
        if (source_width == 8 && source_height == 8 && kind < 4) check_sampling(texture);
    }
    /* Ordinary image users retain straight-alpha pixels. */
    BongoCatImage original = {0};
    CHECK(bongo_cat_image_load(path, &original, &error) == BONGO_CAT_OK);
    if (original.pixels) {
        CHECK(original.width == source_width && original.height == source_height);
        if (kind < 4) {
            CHECK(original.pixels[0] == 255);
            CHECK(original.pixels[3] == (kind == 2 ? 64 : 255));
        }
        if (texture) check_base_pixels(texture, &original);
        if (texture) {
            BongoCatImageAlphaMask expected;
            bongo_cat_image_make_alpha_mask(&original, &expected);
            CHECK(memcmp(&alpha, &expected, sizeof(alpha)) == 0);
        }
        bongo_cat_image_free(&original);
    }
    if (texture) glDeleteTextures(1, &texture);
}

#ifdef _WIN32
static bool cancel_rows(void *userdata, BongoCatImage *rows, int height, int y) {
    int *calls = userdata;
    CHECK(SDL_GL_GetCurrentContext() != NULL);
    CHECK(rows->width == 193 && height == 257 && y == *calls * 64);
    CHECK(rows->height == 64);
    ++*calls;
    return *calls < 2;
}

static void stream_cancel_case(const char *path) {
    fixture(path, 193, 257, 4);
    int calls = 0;
    CHECK(!bongo_cat_image_decode_wic_rows(path, cancel_rows, &calls, NULL, NULL));
    CHECK(calls == 2);
}
#endif

static void hardware_limit_case(const char *path, int limit, bool direct) {
    fixture(path, limit + 1, 1, 0);
    BongoCatError error = {0};
    BongoCatImageAlphaMask alpha;
    memset(&alpha, 0xff, sizeof(alpha));
    int width = -1, height = -1;
    GLuint texture = bongo_cat_image_texture_model(path, direct,
        &width, &height, &alpha, NULL, NULL, &error);
    CHECK(texture == 0 && error.code == BONGO_CAT_ERROR_PLATFORM);
    CHECK(strstr(error.message, "exceeds the GPU limit") != NULL);
    CHECK(width == 0 && height == 0 && alpha.width == 0 && alpha.height == 0);
    CHECK(glGetError() == GL_NO_ERROR);
    if (texture) glDeleteTextures(1, &texture);
}

static void decoder_fallback_case(const char *path) {
    // Gray+alpha is outside the RGB/RGBA streaming fast path. The fallback
    // must still preserve original texels, alpha occupancy and progress.
    enum { width = 193, height = 131 };
    unsigned char pixels[width * height * 2];
    for (size_t i = 0; i < sizeof(pixels); ++i)
        pixels[i] = (unsigned char)((i * 17 + i / 7) % 256);
    CHECK(stbi_write_png(path, width, height, 2, pixels, width * 2));
    BongoCatError error = {0};
    BongoCatImage original = {0};
    CHECK(bongo_cat_image_load(path, &original, &error) == BONGO_CAT_OK);
    ProgressState progress = {0};
    BongoCatImageAlphaMask alpha, expected;
    GLuint texture = bongo_cat_image_texture_model(path, false,
        NULL, NULL, &alpha, check_progress, &progress, &error);
    CHECK(texture != 0 && progress.last == 1.0f);
    if (texture && original.pixels) {
        check_base_pixels(texture, &original);
        check_levels(texture, width, height, false);
        bongo_cat_image_make_alpha_mask(&original, &expected);
        CHECK(memcmp(&alpha, &expected, sizeof(alpha)) == 0);
    }
    if (texture) glDeleteTextures(1, &texture);
    bongo_cat_image_free(&original);
}

static void upload_state_case(const char *path) {
    BongoCatGL gl;
    BongoCatError error = {0};
    if (!bongo_cat_gl_load(&gl, &error)) { CHECK(false); return; }
    fixture(path, 193, 131, 4);
    GLuint previous = 0, unpack = 0;
    PFNGLGENFRAMEBUFFERSPROC generate =
        (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    PFNGLBINDFRAMEBUFFERPROC bind =
        (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    PFNGLDELETEFRAMEBUFFERSPROC remove =
        (PFNGLDELETEFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteFramebuffers");
    CHECK(generate && bind && remove);
    if (!generate || !bind || !remove) return;
    GLuint framebuffers[2] = {0};
    generate(2, framebuffers);
    bind(GL_READ_FRAMEBUFFER, framebuffers[0]);
    bind(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
    glReadBuffer(GL_NONE);
    glEnable(GL_FRAMEBUFFER_SRGB);
    gl.active_texture(GL_TEXTURE3);
    glGenTextures(1, &previous);
    glBindTexture(GL_TEXTURE_2D, previous);
    gl.gen_buffers(1, &unpack);
    gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER, unpack);
    gl.buffer_data(GL_PIXEL_UNPACK_BUFFER, 16, NULL, GL_STATIC_DRAW);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 29);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 7);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 3);
    for (int kind = 0; kind < 5; ++kind) {
        GLuint texture = kind < 2 ? bongo_cat_image_texture_model(path, kind == 0,
            NULL, NULL, NULL, NULL, NULL, &error) : kind == 2 ?
            bongo_cat_image_texture(path, NULL, NULL, &error) : kind == 3 ?
            bongo_cat_image_texture_resampled(path, 8, 8, 0, NULL, NULL, &error) :
            bongo_cat_image_texture_thumbnail(path, 4, 4, NULL, NULL, &error);
        CHECK(texture != 0);
        GLint actual = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &actual); CHECK(actual == GL_TEXTURE3);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual); CHECK((GLuint)actual == previous);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &actual); CHECK((GLuint)actual == unpack);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &actual); CHECK(actual == 8);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &actual); CHECK(actual == 29);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &actual); CHECK(actual == 7);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &actual); CHECK(actual == 3);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &actual);
        CHECK((GLuint)actual == framebuffers[0]);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &actual);
        CHECK((GLuint)actual == framebuffers[1]);
        glGetIntegerv(GL_READ_BUFFER, &actual); CHECK(actual == GL_NONE);
        CHECK(glIsEnabled(GL_FRAMEBUFFER_SRGB));
        if (texture) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &actual);
            CHECK(actual == (kind < 2 ? GL_REPEAT : GL_CLAMP_TO_EDGE));
            glBindTexture(GL_TEXTURE_2D, previous);
            if (kind < 3) {
                BongoCatImage original = {0};
                CHECK(bongo_cat_image_load(path, &original, &error) == BONGO_CAT_OK);
                if (original.pixels) {
                    bool saved = straight_alpha;
                    straight_alpha = kind >= 2;
                    check_base_pixels(texture, &original);
                    straight_alpha = saved;
                    bongo_cat_image_free(&original);
                    glBindTexture(GL_TEXTURE_2D, previous);
                }
            }
            glDeleteTextures(1, &texture);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    gl.bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0);
    gl.delete_buffers(1, &unpack);
    glDeleteTextures(1, &previous);
    gl.active_texture(GL_TEXTURE0);
    glDisable(GL_FRAMEBUFFER_SRGB);
    bind(GL_FRAMEBUFFER, 0);
    remove(2, framebuffers);
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
        GLint limit = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
        const int sizes[][2] = {{8, 8}, {19, 7}, {1, 7}, {193, 131}, {129, 257},
            {4096, 8}, {8, 4096}, {8192, 8}};
        for (size_t i = 0; i < SDL_arraysize(sizes); ++i) {
            if (sizes[i][0] > limit || sizes[i][1] > limit) continue;
            for (int kind = 0; kind < 5; ++kind) {
                model_case(path, sizes[i][0], sizes[i][1], kind, true);
                model_case(path, sizes[i][0], sizes[i][1], kind, false);
            }
        }
        if (limit > 0 && limit <= 65536) {
            hardware_limit_case(path, limit, true);
            hardware_limit_case(path, limit, false);
        }
        upload_state_case(path);
        decoder_fallback_case(path);
#ifdef _WIN32
        stream_cancel_case(path);
#endif
        CHECK(SDL_RemovePath(path));
        SDL_free(path);
    }
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
