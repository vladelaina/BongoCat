#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FrameStats {
    unsigned visible;
    unsigned alpha;
} FrameStats;

static FrameStats frame_stats(const unsigned char *pixels,
    int width, int height, size_t pitch) {
    FrameStats stats = {0};
    for (int y = 0; y < height; y += 4) {
        const unsigned char *row = pixels + (size_t)y * pitch;
        for (int x = 0; x < width; x += 4) {
            const unsigned char *pixel = row + (size_t)x * 4;
            if ((unsigned)pixel[0] + pixel[1] + pixel[2] > 60) stats.visible++;
            if (pixel[3] > 8) stats.alpha++;
        }
    }
    return stats;
}

static void log_first_frame(BongoCatApp *app, int width, int height) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "First-frame diagnostic skipped for size %dx%d", width, height);
        return;
    }
    size_t pitch = (size_t)width * 4, bytes = pitch * (size_t)height;
    if (pitch / 4 != (size_t)width || bytes / pitch != (size_t)height) return;
    unsigned char *pixels = calloc(1, bytes);
    if (!pixels) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "First-frame diagnostic allocation failed for %zu bytes", bytes);
        return;
    }
    GLenum before = glGetError();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum after = glGetError();
    unsigned long long alpha = 0, opaque = 0, rgb_colored = 0;
    unsigned long long visible_colored = 0, black = 0;
    unsigned long long red = 0, green = 0, blue = 0, alpha_sum = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const unsigned char *pixel = pixels + (size_t)y * pitch + (size_t)x * 4;
        unsigned sum = (unsigned)pixel[0] + pixel[1] + pixel[2];
        alpha += pixel[3] > 8; opaque += pixel[3] > 247;
        rgb_colored += sum > 30;
        visible_colored += pixel[3] > 8 && sum > 30;
        black += pixel[3] > 8 && sum <= 30;
        red += pixel[0]; green += pixel[1]; blue += pixel[2];
        alpha_sum += pixel[3];
    }
    unsigned long long total = (unsigned long long)width * height;
    int sample_buffers = 0, sample_count = 0;
    SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &sample_buffers);
    SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &sample_count);
    SDL_Log("First-frame pixels: size=%dx%d total=%llu alpha=%llu opaque=%llu "
        "rgb_colored=%llu visible_colored=%llu black_with_alpha=%llu "
        "transparent=%llu "
        "avg_rgba=%.1f,%.1f,%.1f,%.1f gl_error_before=0x%x gl_error_after=0x%x "
        "msaa=%d/%d", width, height, total, alpha, opaque, rgb_colored,
        visible_colored, black,
        total - alpha, (double)red / total, (double)green / total,
        (double)blue / total, (double)alpha_sum / total, before, after,
        sample_buffers, sample_count);
    SDL_Log("First-frame ratios: alpha=%.2f%% rgb_colored=%.2f%% "
        "visible_colored=%.2f%% black_with_alpha=%.2f%%",
        100.0 * alpha / total, 100.0 * rgb_colored / total,
        100.0 * visible_colored / total, 100.0 * black / total);
    if (after != GL_NO_ERROR) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "First-frame diagnosis: OpenGL framebuffer readback failed (0x%x)", after);
    else if (!rgb_colored && !alpha) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "First-frame diagnosis: framebuffer is blank and fully transparent");
    else if (!rgb_colored) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "First-frame diagnosis: framebuffer contains only black pixels");
    else if (!visible_colored) SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
        "First-frame diagnosis: framebuffer has RGB content but no visible color alpha");
    else SDL_Log("First-frame diagnosis: OpenGL framebuffer contains visible content");
    bongo_cat_frame_presentation_prepare(app, pixels, width, height,
        after == GL_NO_ERROR && visible_colored > 0);
    if (app->smoke) {
        const int points[][2] = {{0, 0}, {width - 1, 0}, {0, height - 1},
            {width - 1, height - 1}, {width / 2, height / 2}};
        unsigned transparent = 0, opaque_samples = 0;
        for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
            unsigned char value = pixels[(size_t)points[i][1] * pitch +
                (size_t)points[i][0] * 4 + 3];
            transparent += value < 16; opaque_samples += value > 239;
        }
        char path[BONGO_CAT_PATH_CAP];
        bongo_cat_path_join(path, sizeof(path), app->state_root,
            "frame-alpha.txt");
        FILE *file = bongo_cat_file_open(path, "wb");
        if (file) {
            fprintf(file, "samples=5 transparent=%u opaque=%u sample_buffers=%d "
                "sample_count=%d gl_error=%u\n", transparent, opaque_samples,
                sample_buffers, sample_count, (unsigned)after);
            fclose(file);
        }
    }
    free(pixels);
}

static void record_frame(BongoCatApp *app, const unsigned char *pixels,
    int width, int height, size_t pitch, GLenum before, GLenum after) {
    if (!app->smoke_frame_series) return;
    char path[BONGO_CAT_PATH_CAP];
    bongo_cat_path_join(path, sizeof(path), app->state_root,
        "frame-series.csv");
    bool header = !bongo_cat_path_is_file(path);
    FILE *file = bongo_cat_file_open(path, "ab");
    if (!file) return;
    if (header) fputs("ticks_ns,width,height,visible_pixels,alpha_pixels,"
        "scale_percent,opacity_percent,window_opacity,model_mode,"
        "model_state_consistent,selection_serial,window_config_visible,"
        "window_os_visible,loaded_model,active_model,runtime_stage,context_current,"
        "gl_error_before,gl_error_after\n", file);
    FrameStats stats = frame_stats(pixels, width, height, pitch);
    bool model_consistent = bongo_cat_live2d_ready(app->live2d) &&
        app->loaded_model[0] &&
        strcmp(app->loaded_model, app->session.active_model_id) == 0;
    bool os_visible = (SDL_GetWindowFlags(app->window) & SDL_WINDOW_HIDDEN) == 0;
    fprintf(file, "%llu,%d,%d,%u,%u,%.3f,%.3f,%.5f,%s,%d,%u,%d,%d,%s,%s,%u,%d,%u,%u\n",
        (unsigned long long)SDL_GetTicksNS(), width, height,
        stats.visible, stats.alpha,
        app->session.window.scale_percent, app->session.window.opacity_percent,
        bongo_cat_platform_get_opacity(&app->platform),
        bongo_cat_mode_name(app->loaded_mode), model_consistent,
        app->model_selection_serial, app->session.window.visible, os_visible,
        app->loaded_model, app->session.active_model_id, app->smoke_runtime_stage,
        SDL_GL_GetCurrentContext() == app->gl_context, (unsigned)before, (unsigned)after);
    fclose(file);
}

void bongo_cat_frame_audit(BongoCatApp *app, int width, int height) {
    if (!app || !app->smoke || width < 2 || height < 2) return;
    if (!app->smoke_frame_audited) {
        app->smoke_frame_audited = true;
        log_first_frame(app, width, height);
    }
    char path[BONGO_CAT_PATH_CAP];
    size_t pitch = (size_t)width * 4, bytes = pitch * (size_t)height;
    unsigned char *pixels = malloc(bytes);
    if (!pixels) return;
    GLenum before = glGetError();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum after = glGetError();
    record_frame(app, pixels, width, height, pitch, before, after);
    uint64_t now = SDL_GetTicksNS();
    if (app->frame_audit_bmp_ns && now >= app->frame_audit_bmp_ns &&
        now - app->frame_audit_bmp_ns < 50000000ull) {
        free(pixels); return;
    }
    app->frame_audit_bmp_ns = now;
    unsigned char *row = malloc(pitch);
    if (!row) { free(pixels); return; }
    for (int y = 0; y < height / 2; ++y) {
        unsigned char *top = pixels + (size_t)y * pitch;
        unsigned char *bottom = pixels + (size_t)(height - 1 - y) * pitch;
        memcpy(row, top, pitch); memcpy(top, bottom, pitch); memcpy(bottom, row, pitch);
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height,
        SDL_PIXELFORMAT_RGBA32, pixels, (int)pitch);
    bongo_cat_path_join(path, sizeof(path), app->state_root, "frame.bmp");
    if (surface) { SDL_SaveBMP(surface, path); SDL_DestroySurface(surface); }
    free(row); free(pixels);
}
