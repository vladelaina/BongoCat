#define bongo_cat_image_texture_model test_image_texture_model
#define bongo_cat_image_texture_model_scaled test_image_texture_model_scaled
#define bongo_cat_image_texture_model_scaled_cached test_image_texture_model_scaled_cached
#define bongo_cat_image_upload_texture test_image_upload_texture
#define bongo_cat_image_release_upload_buffer test_image_release_upload_buffer
#define bongo_cat_image_begin_model_texture test_image_begin_model_texture
#define bongo_cat_image_upload_model_rows test_image_upload_model_rows
#define bongo_cat_image_upload_model_rows_prepared test_image_upload_model_rows_prepared
#define bongo_cat_image_finish_model_texture test_image_finish_model_texture
#include "image_internal.h"
#include "bongo_cat/gl_api.h"
#include "test.h"
#include <SDL3/SDL.h>
#include <stb_image_write.h>
#include <string.h>

int bongo_cat_test_failures;
static unsigned failed_uploads;
static unsigned failed_storage;
static bool inject_storage_failure;

static void APIENTRY fail_after_storage(GLenum target, GLsizei levels,
    GLenum format, GLsizei width, GLsizei height) {
    PFNGLTEXSTORAGE2DPROC storage =
        (PFNGLTEXSTORAGE2DPROC)SDL_GL_GetProcAddress("glTexStorage2D");
    storage(target, levels, format, width, height);
    CHECK(glGetError() == GL_NO_ERROR);
    ++failed_storage;
    glBindTexture(0, 0); // Inject a GL error after immutable storage exists.
}

static SDL_FunctionPointer test_proc(const char *name) {
    SDL_FunctionPointer result = SDL_GL_GetProcAddress(name);
    return result && inject_storage_failure && !strcmp(name, "glTexStorage2D") ?
        (SDL_FunctionPointer)fail_after_storage : result;
}

static bool fail_after_mip_allocation(const BongoCatImage *image) {
    CHECK(bongo_cat_image_upload_mipmaps(image));
    ++failed_uploads;
    return false;
}

static bool fail_after_mip_generation(void) {
    CHECK(bongo_cat_image_generate_mipmaps());
    ++failed_uploads;
    return false;
}

/* Exercise recovery after driver-side mip storage exists, without exhausting
   actual GPU memory. All decoding, texture allocation and readback are real. */
#define bongo_cat_image_upload_mipmaps fail_after_mip_allocation
#define bongo_cat_image_generate_mipmaps fail_after_mip_generation
#define SDL_GL_GetProcAddress test_proc
#include "../../src/media/image_upload.c"
#include "../../src/media/image_model.c"

static void check_fallback(const char *path, bool direct) {
    const unsigned char pixels[] = {
        255, 128, 64, 64, 255, 0, 255, 0, 17, 93, 241, 255, 128, 64, 32, 128};
    const unsigned char expected[] = {
        64, 32, 16, 64, 0, 0, 0, 0, 17, 93, 241, 255, 64, 32, 16, 128};
    CHECK(stbi_write_png(path, 4, 1, 4, pixels, 16));
    BongoCatError error = {0};
    BongoCatImageAlphaMask alpha;
    int width = 0, height = 0;
    GLuint texture = test_image_texture_model(path, direct,
        &width, &height, &alpha, NULL, NULL, &error);
    CHECK(texture != 0);
    if (!texture) return;
    CHECK(width == 4 && height == 1 && alpha.width == 4 && alpha.height == 1);
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint actual_width = 0, actual_height = 0, filter = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &actual_width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &actual_height);
    CHECK(actual_width == 4 && actual_height == 1);
    if (actual_width == 4 && actual_height == 1) {
        unsigned char actual[sizeof(expected)] = {0};
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, actual);
        CHECK(memcmp(actual, expected, sizeof(expected)) == 0);
    }
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &actual_width);
    CHECK(actual_width == 0);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
    CHECK(filter == GL_LINEAR);
    CHECK(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &texture);
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("Image upload recovery", 32, 32,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    CHECK(window != NULL);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    CHECK(context != NULL);
    char *path = NULL;
    if (context) {
        SDL_asprintf(&path, "%sbongocat-filter-recovery-%llu.png", SDL_GetBasePath(),
            (unsigned long long)SDL_GetTicksNS());
        CHECK(path != NULL);
        if (path) {
            check_fallback(path, true);
            check_fallback(path, false);
            CHECK(failed_uploads == 2);
            if (SDL_GL_ExtensionSupported("GL_ARB_texture_storage")) {
                inject_storage_failure = true;
                check_fallback(path, false);
                CHECK(failed_storage == 1 && failed_uploads == 3);
            }
            CHECK(SDL_RemovePath(path));
            SDL_free(path);
        }
        SDL_GL_DestroyContext(context);
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
