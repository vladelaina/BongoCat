#include "bongo_cat/overlay.h"
#include "bongo_cat/path.h"
#include "test.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <stb_image_write.h>
#include <stdlib.h>
#include <string.h>

int bongo_cat_test_failures;

static void check_pixel(bool filled) {
    unsigned char pixel[4] = {0};
    glReadPixels(28, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(filled ? pixel[0] > 250 : pixel[0] == 0);
    CHECK(glGetError() == GL_NO_ERROR);
}

static void layout_case(BongoCatOverlay *overlay, const char *root,
    const char *background, int width, int height, int reference_width,
    int reference_height, bool mver, bool filled) {
    size_t bytes = (size_t)width * height * 4;
    unsigned char *pixels = malloc(bytes);
    CHECK(pixels != NULL);
    if (!pixels) return;
    memset(pixels, 255, bytes);
    CHECK(stbi_write_png(background, width, height, 4, pixels, width * 4));
    free(pixels);
    BongoCatLive2DRenderOptions options = {0};
    options.mver_projection = mver;
    options.reference_width = reference_width;
    options.reference_height = reference_height;
    BongoCatError error = {0};
    CHECK(bongo_cat_overlay_load(overlay, root, true, &options, &error) == BONGO_CAT_OK);
    glClear(GL_COLOR_BUFFER_BIT);
    bongo_cat_overlay_draw_background(overlay, false);
    check_pixel(filled);
    /* Effects must use the same logical canvas as the background. */
    CHECK(bongo_cat_overlay_effect(overlay, background));
    glClear(GL_COLOR_BUFFER_BIT);
    bongo_cat_overlay_draw_effect(overlay, false);
    check_pixel(filled);
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *window = SDL_CreateWindow("Overlay layout test", 32, 32,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
    CHECK(context != NULL);
    if (!context) { SDL_Quit(); return 1; }
    char root[BONGO_CAT_PATH_CAP], resources[BONGO_CAT_PATH_CAP];
    char background[BONGO_CAT_PATH_CAP];
    SDL_snprintf(root, sizeof(root), "%soverlay-layout-%llu", SDL_GetBasePath(),
        (unsigned long long)SDL_GetTicksNS());
    CHECK(bongo_cat_path_join(resources, sizeof(resources), root, "resources"));
    CHECK(bongo_cat_path_join(background, sizeof(background), resources, "background.png"));
    CHECK(SDL_CreateDirectory(resources));
    BongoCatError error = {0};
    BongoCatOverlay *overlay = bongo_cat_overlay_create(&error);
    CHECK(overlay != NULL);
    glViewport(0, 0, 32, 32);
    glClearColor(0, 0, 0, 0);
    if (overlay) {
        layout_case(overlay, root, background, 612, 354, 1224, 708, true, true);
        layout_case(overlay, root, background, 612, 352, 1836, 1056, true, true);
        layout_case(overlay, root, background, 612, 354, 612, 354, true, true);
        layout_case(overlay, root, background, 612, 354, 1400, 1400, true, false);
        layout_case(overlay, root, background, 306, 177, 612, 354, true, false);
        layout_case(overlay, root, background, 612, 354, 1224, 354, true, false);
        layout_case(overlay, root, background, 612, 354, 1224, 708, false, true);
        bongo_cat_overlay_destroy(overlay);
    }
    CHECK(SDL_RemovePath(background));
    CHECK(SDL_RemovePath(resources));
    CHECK(SDL_RemovePath(root));
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return bongo_cat_test_failures ? 1 : 0;
}
