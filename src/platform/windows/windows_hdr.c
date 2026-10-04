#include "windows_hdr.h"
#include "windows_layered.h"
#include "windows_capture.h"

static const char ui_presenter_property[] = "BongoCat.HDRPresenter";

void bongo_cat_windows_prepare_transparent_ui(SDL_Window *window) {
    if (!window || !(SDL_GetWindowFlags(window) & SDL_WINDOW_TRANSPARENT)) return;
    HWND handle = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    bongo_cat_windows_capture_mark_transparent(handle, true);
    bongo_cat_windows_capture_install_transparency_handler(handle);
    bongo_cat_windows_capture_repair_transparency(handle);
}

bool bongo_cat_windows_hdr_enabled(SDL_Window *window) {
    if (!window || !(SDL_GetWindowFlags(window) & SDL_WINDOW_TRANSPARENT)) return false;
    /* An explicit diagnostic override lets the same regression exercise SDR
       and HDR paths on one machine. Normal launches use the display state. */
    const char *override = SDL_GetHint("BONGO_CAT_TEST_HDR_PRESENTATION");
    if (override) return SDL_GetHintBoolean("BONGO_CAT_TEST_HDR_PRESENTATION", false);
    int native_hdr = bongo_cat_windows_display_hdr(window);
    if (native_hdr >= 0) return native_hdr != 0;
    /* SDL's window HDR cache is initialized on creation. In the bundled SDL,
       moving to another display does not refresh it until HDR itself changes.
       Menus are created first and then positioned at the pointer. */
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    SDL_PropertiesID properties = display ? SDL_GetDisplayProperties(display) : 0;
    if (properties && SDL_GetBooleanProperty(properties,
            SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN, false)) return true;
    return SDL_GetBooleanProperty(SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
}

static void SDLCALL release_ui_presenter(void *userdata, void *pointer) {
    (void)userdata;
    BongoCatPlatform *platform = pointer;
    bongo_cat_windows_layered_destroy(platform);
    SDL_free(platform);
}

bool bongo_cat_windows_hdr_present(SDL_Window *window, int width, int height) {
    /* UI callers paint once before showing their window, unlike the pet's
       explicit show-and-retry sequence. Keep that first WGL frame intact. */
    if (SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED))
        return SDL_GL_SwapWindow(window);
    SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    BongoCatPlatform *platform = SDL_GetPointerProperty(properties,
        ui_presenter_property, NULL);
    if (!platform && !bongo_cat_windows_hdr_enabled(window)) return SDL_GL_SwapWindow(window);
    if (!platform) {
        platform = SDL_calloc(1, sizeof(*platform));
        if (!platform) return SDL_GL_SwapWindow(window);
        platform->window = window;
        platform->window_opacity = 1.0f;
        platform->presenter = bongo_cat_windows_layered_create(false);
        if (!platform->presenter) {
            SDL_free(platform);
            return SDL_GL_SwapWindow(window);
        }
        if (!SDL_SetPointerPropertyWithCleanup(properties, ui_presenter_property,
            platform, release_ui_presenter, NULL)) return SDL_GL_SwapWindow(window);
    }
    return bongo_cat_platform_present(platform, width, height);
}
