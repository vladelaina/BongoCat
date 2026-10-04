#include "runtime.h"

#include <SDL3/SDL_opengl.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#include "windows_capture.h"
#include "windows_diagnostics.h"
#include "windows_layered.h"
#include <SDL3/SDL_properties.h>
#endif

#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_SAMPLE_BUFFERS
#define GL_SAMPLE_BUFFERS 0x80A8
#endif
#ifndef GL_SAMPLES
#define GL_SAMPLES 0x80A9
#endif

typedef struct PresentationAudit {
    bool prepared;
    bool presented;
    bool visible;
    int width;
    int height;
    uint64_t rendered_hash;
} PresentationAudit;

static PresentationAudit audit;

static uint64_t pixel_hash(const unsigned char *pixels, size_t bytes) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t index = 0; index < bytes; ++index) {
        hash ^= pixels[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void log_gl_presentation_state(BongoCatApp *app) {
    GLint draw_buffer = 0, read_buffer = 0;
    GLint draw_framebuffer = 0, read_framebuffer = 0;
    GLint red_bits = 0, green_bits = 0, blue_bits = 0, alpha_bits = 0;
    GLint depth_bits = 0, stencil_bits = 0, sample_buffers = 0, samples = 0;
    GLboolean double_buffer = GL_FALSE, stereo = GL_FALSE;
    GLenum before = glGetError();
    glGetIntegerv(GL_DRAW_BUFFER, &draw_buffer);
    glGetIntegerv(GL_READ_BUFFER, &read_buffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
    /* SDL uses attachment queries for sizes removed from core glGetIntegerv. */
    SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &red_bits);
    SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &green_bits);
    SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &blue_bits);
    SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &alpha_bits);
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depth_bits);
    SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencil_bits);
    glGetIntegerv(GL_SAMPLE_BUFFERS, &sample_buffers);
    glGetIntegerv(GL_SAMPLES, &samples);
    glGetBooleanv(GL_DOUBLEBUFFER, &double_buffer);
    glGetBooleanv(GL_STEREO, &stereo);
    GLenum after = glGetError();
    int sdl_double_buffer = -1, accelerated = -1, retained = -1;
    SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &sdl_double_buffer);
    SDL_GL_GetAttribute(SDL_GL_ACCELERATED_VISUAL, &accelerated);
    SDL_GL_GetAttribute(SDL_GL_RETAINED_BACKING, &retained);
    SDL_WindowFlags flags = app && app->window ?
        SDL_GetWindowFlags(app->window) : 0;
    SDL_Log("OpenGL presentation state: draw_buffer=0x%x read_buffer=0x%x "
        "draw_fbo=%d read_fbo=%d rgba_bits=%d,%d,%d,%d depth_bits=%d "
        "stencil_bits=%d sample_buffers=%d samples=%d double_buffer=%d "
        "stereo=%d sdl_double_buffer=%d accelerated=%d retained=%d "
        "window_flags=0x%llx state_error_before=0x%x state_error_after=0x%x",
        draw_buffer, read_buffer, draw_framebuffer, read_framebuffer,
        red_bits, green_bits, blue_bits, alpha_bits, depth_bits, stencil_bits,
        sample_buffers, samples, double_buffer != GL_FALSE,
        stereo != GL_FALSE, sdl_double_buffer, accelerated, retained,
        (unsigned long long)flags, before, after);
}

void bongo_cat_frame_presentation_prepare(BongoCatApp *app,
    const unsigned char *pixels, int width, int height, bool visible) {
    if (!app || !pixels || width < 1 || height < 1) return;
    size_t bytes = (size_t)width * (size_t)height * 4;
    if (bytes / 4 != (size_t)width * (size_t)height) return;
    audit = (PresentationAudit){.prepared = true, .visible = visible,
        .width = width, .height = height,
        .rendered_hash = pixel_hash(pixels, bytes)};
    SDL_Log("Pre-presentation framebuffer: size=%dx%d visible=%d "
        "rgba_hash=0x%016llx", width, height, visible,
        (unsigned long long)audit.rendered_hash);
    log_gl_presentation_state(app);
}

static void log_post_present_buffer(const char *name, GLenum buffer) {
    size_t total = (size_t)audit.width * (size_t)audit.height;
    size_t bytes = total * 4;
    unsigned char *pixels = calloc(1, bytes);
    if (!pixels) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Post-presentation %s allocation failed for %zu bytes", name, bytes);
        return;
    }
    GLint previous = GL_BACK;
    glGetIntegerv(GL_READ_BUFFER, &previous);
    GLenum before = glGetError();
    glReadBuffer(buffer);
    glReadPixels(0, 0, audit.width, audit.height,
        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum after = glGetError();
    glReadBuffer((GLenum)previous);
    GLenum restore_error = glGetError();
    unsigned long long alpha = 0, visible = 0, nonblack = 0;
    for (size_t index = 0; index < bytes; index += 4) {
        unsigned sum = (unsigned)pixels[index] + pixels[index + 1] +
            pixels[index + 2];
        alpha += pixels[index + 3] > 8;
        nonblack += sum > 30;
        visible += pixels[index + 3] > 8 && sum > 30;
    }
    uint64_t hash = pixel_hash(pixels, bytes);
    bool valid = after == GL_NO_ERROR;
    SDL_Log("Post-presentation OpenGL buffer: name=%s enum=0x%x size=%dx%d "
        "valid=%d alpha=%.2f%% nonblack=%.2f%% visible=%.2f%% "
        "rgba_hash=0x%016llx matches_pre_swap=%d error_before=0x%x "
        "error_after=0x%x restore_error=0x%x",
        name, buffer, audit.width, audit.height, valid,
        100.0 * alpha / total, 100.0 * nonblack / total,
        100.0 * visible / total, (unsigned long long)hash,
        valid && hash == audit.rendered_hash, before, after, restore_error);
    free(pixels);
}

void bongo_cat_frame_presented_audit(BongoCatApp *app) {
    if (!app || !app->smoke || audit.presented || !audit.prepared) return;
    audit.presented = true;
    SDL_Log("First-frame presentation: success=1 framebuffer_visible=%d "
        "window_opacity=%.3f obs_background=%d obs_background_color=%s",
        audit.visible, bongo_cat_platform_get_opacity(&app->platform),
        app->settings.window.obs_background,
        bongo_cat_obs_background_color_name(
            app->settings.window.obs_background_color));
#ifdef _WIN32
    HWND source = app->window ? (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(app->window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL) : NULL;
    if (!source) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Capture diagnostic skipped because the Win32 window is unavailable");
        return;
    }
    HWND proxy = bongo_cat_windows_layered_proxy(source);
    bool proxy_visible = proxy && IsWindow(proxy) && IsWindowVisible(proxy);
    SDL_Log("Windows presented path: source=%p proxy=%p proxy_visible=%d",
        (void *)source, (void *)proxy, proxy_visible);
    if (!proxy_visible) {
        log_post_present_buffer("front", GL_FRONT);
        log_post_present_buffer("back", GL_BACK);
    } else {
        SDL_Log("Post-presentation OpenGL front-buffer readback skipped: "
            "the visible output is the layered proxy");
    }
    bongo_cat_windows_capture_log(source, "first-presented-source");
    bongo_cat_windows_diagnostics_probe_capture(source, "opengl-source");
    if (proxy && IsWindow(proxy)) {
        bongo_cat_windows_capture_log(proxy, "first-presented-layered-proxy");
        bongo_cat_windows_diagnostics_probe_capture(proxy, "layered-proxy");
    }
#else
    log_post_present_buffer("front", GL_FRONT);
    log_post_present_buffer("back", GL_BACK);
#endif
}
