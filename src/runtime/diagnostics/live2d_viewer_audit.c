#include "runtime.h"
#include "live2d_viewer_timing.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *metric_parameters[] = {
    "ParamAngleX", "ParamAngleY", "ParamEyeBallX", "ParamEyeBallY",
    "ParamEyeLOpen", "ParamEyeROpen",
    "ParamBodyAngleX", "ParamBreath", "ParamAngleZ", "ParamMouseX", "ParamMouseY",
    "ParamMouseLeftDown", "ParamMouseRightDown", "CatParamLeftHandDown",
    "hair1", "hair2", "hair3", "hair4", "hair5", "hair6",
    "hair10", "hair11", "hair12", "hair13",
    "chest1", "chest3", "chest4", "Param", "Param18", "Param19", "Param20",
    "Param24", "Param25", "Param26",
    "Param28", "RE1", "RE2", "LE1", "LE2"
};
static const char *track_names[] = {"track-001", "track-002", "track-004",
    "track-008", "track-015", "track-030"};
static const char *return_names[] = {"return-001", "return-002", "return-004",
    "return-008", "return-015", "return-030"};
static bool parameter(BongoCatApp *app, const char *id, float *value) {
    BongoCatParameterRange range;
    if (!bongo_cat_live2d_parameter(app->live2d, id, &range)) return false;
    *value = range.value;
    return true;
}
static bool parameter_range(BongoCatApp *app, const char *id,
    BongoCatParameterRange *range) {
    return bongo_cat_live2d_parameter(app->live2d, id, range);
}
static float viewer_target(const BongoCatParameterRange *range,
    float direction, float weight) {
    float extent = direction < 0.0f ? fabsf(range->minimum) :
        fabsf(range->maximum);
    return direction * extent * weight;
}
static bool parameter_near(BongoCatApp *app, const char *id,
    float expected, float tolerance) {
    float actual = 0.0f;
    return parameter(app, id, &actual) && fabsf(actual - expected) <= tolerance;
}
static bool save_frame(BongoCatApp *app, const char *directory,
    const char *name, FILE *metrics) {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(app->window, &width, &height);
    if (width < 2 || height < 2) return false;
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    bongo_cat_live2d_draw(app->live2d);
    glFinish();
    size_t pitch = (size_t)width * 4;
    unsigned char *pixels = malloc(pitch * (size_t)height);
    unsigned char *row = malloc(pitch);
    if (!pixels || !row) { free(pixels); free(row); return false; }
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char *top = pixels + (size_t)y * pitch;
        unsigned char *bottom = pixels + (size_t)(height - 1 - y) * pitch;
        memcpy(row, top, pitch); memcpy(top, bottom, pitch); memcpy(bottom, row, pitch);
    }
    SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height,
        SDL_PIXELFORMAT_RGBA32, pixels, (int)pitch);
    char filename[64], path[BONGO_CAT_PATH_CAP];
    snprintf(filename, sizeof(filename), "%s.bmp", name);
    bool saved = surface && bongo_cat_path_join(path, sizeof(path), directory,
        filename) && SDL_SaveBMP(surface, path);
    if (surface) SDL_DestroySurface(surface);
    free(row); free(pixels);
    bool required = true;
    if (metrics) {
        fputs(name, metrics);
        for (size_t i = 0; i < sizeof(metric_parameters) /
            sizeof(metric_parameters[0]); ++i) {
            float value = 0.0f;
            bool available = parameter(app, metric_parameters[i], &value);
            if (i < 4) required = available && required;
            if (available) fprintf(metrics, ",%.6f", value);
            else fputs(",nan", metrics);
        }
        BongoCatLive2DVisualState visual = {0};
        bool visual_ready = bongo_cat_live2d_visual_state(app->live2d, &visual);
        if (visual_ready) fprintf(metrics, ",%d,%d,%d,%d,%d,%d,%d",
            visual.drawable_count, visual.drawable_visible,
            visual.drawable_vertex_changed, visual.offscreen_count,
            visual.offscreen_positive, visual.part_count, visual.part_positive);
        else fputs(",nan,nan,nan,nan,nan,nan,nan", metrics);
        fprintf(metrics, ",%d\n", required);
    }
    return saved && required;
}
static void render_step(BongoCatApp *app) {
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(app->window, &width, &height);
    if (width < 2 || height < 2) return;
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    bongo_cat_live2d_draw(app->live2d);
    if (!bongo_cat_platform_present(&app->platform, width, height))
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Viewer audit frame presentation failed: %s", SDL_GetError());
}
static void advance(BongoCatApp *app, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        bongo_cat_live2d_update(app->live2d, 1.0f / 60.0f);
        render_step(app);
    }
}
static bool save_timed(BongoCatApp *app, const char *directory,
    const char *name, int frame, FILE *metrics, FILE *timing) {
    if (timing) fprintf(timing, "%s,%d,%.3f\n", name, frame,
        frame * (1000.0 / 60.0));
    return save_frame(app, directory, name, metrics);
}
static bool advance_tracking(BongoCatApp *app, int frames,
    float *previous_x, float *maximum_step) {
    for (int frame = 0; frame < frames; ++frame) {
        bongo_cat_live2d_update(app->live2d, 1.0f / 60.0f);
        render_step(app);
        float current_x = 0.0f;
        if (!parameter(app, "ParamAngleX", &current_x)) return false;
        float step = fabsf(current_x - *previous_x);
        if (step > *maximum_step) *maximum_step = step;
        *previous_x = current_x;
    }
    return true;
}

bool bongo_cat_live2d_viewer_audit_run(BongoCatApp *app) {
    if (!app || !app->live2d || !app->window) return false;
    int original_width = 0, original_height = 0;
    SDL_GetWindowSize(app->window, &original_width, &original_height);
    if (!SDL_SetWindowSize(app->window, 900, 900) ||
        !SDL_SyncWindow(app->window)) return false;
    int audit_width = 0, audit_height = 0;
    SDL_GetWindowSizeInPixels(app->window, &audit_width, &audit_height);
    bongo_cat_live2d_resize(app->live2d, audit_width, audit_height);
    char root[BONGO_CAT_PATH_CAP], native[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(root, sizeof(root), app->state_root,
        "cubism-viewer-audit") ||
        !bongo_cat_path_join(native, sizeof(native), root, "native") ||
        !bongo_cat_path_create_directory(root) ||
        !bongo_cat_path_create_directory(native)) return false;
    char metrics_path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(metrics_path, sizeof(metrics_path), root,
        "native-parameters.csv")) return false;
    FILE *metrics = bongo_cat_file_open(metrics_path, "wb");
    if (!metrics) return false;
    char timing_path[BONGO_CAT_PATH_CAP];
    FILE *timing = bongo_cat_path_join(timing_path, sizeof(timing_path), root,
        "native-trace.csv") ? bongo_cat_file_open(timing_path, "wb") : NULL;
    if (!timing) { fclose(metrics); return false; }
    fputs("frame,display_frame,display_ms\n", timing);
    fputs("frame", metrics);
    for (size_t i = 0; i < sizeof(metric_parameters) /
        sizeof(metric_parameters[0]); ++i)
        fprintf(metrics, ",%s", metric_parameters[i]);
    fputs(",drawable_count,drawable_visible,drawable_vertex_changed,"
        "offscreen_count,offscreen_positive,part_count,part_positive,"
        "required_available\n", metrics);
    bongo_cat_live2d_prepare_viewer_audit(app->live2d);
    bool passed = bongo_cat_live2d_set_expression(app->live2d, -1);
    bongo_cat_live2d_set_dragging(app->live2d, 0.0f, 0.0f);
    advance(app, 120);
    const char *expression_id = "Param6";
    float expression_before = 0.0f;
    bool expression_parameter = parameter(app, expression_id, &expression_before);
    if (!expression_parameter) {
        expression_id = "Param5";
        expression_parameter = parameter(app, expression_id, &expression_before);
    }
    if (bongo_cat_live2d_set_expression(app->live2d, 2)) {
        advance(app, 60);
        passed = save_frame(app, native, "track-000", metrics) && passed;
        passed = save_frame(app, native, "idle", metrics) && passed;
        passed = save_frame(app, native, "expression3", metrics) && passed;
        float expression_after = expression_before;
        passed = expression_parameter && parameter(app, expression_id,
            &expression_after) && expression_after > expression_before + 0.5f && passed;
    } else passed = false;

    bongo_cat_live2d_set_dragging(app->live2d, -0.008875728f, 0.0f);
    advance(app, 12);
    const float target_drag_x = 0.497041464f;
    const float target_drag_y = 0.598930478f;
    BongoCatParameterRange angle_x_range, angle_y_range, angle_z_range;
    BongoCatParameterRange eye_x_range, eye_y_range, body_x_range;
    passed = parameter_range(app, "ParamAngleX", &angle_x_range) &&
        parameter_range(app, "ParamAngleY", &angle_y_range) &&
        parameter_range(app, "ParamEyeBallX", &eye_x_range) &&
        parameter_range(app, "ParamEyeBallY", &eye_y_range) && passed;
    bool has_body_x = parameter_range(app, "ParamBodyAngleX", &body_x_range);
    bool has_angle_z = parameter_range(app, "ParamAngleZ", &angle_z_range);
    const float viewer_gain = 1.2f;
    const float target_x = viewer_target(&angle_x_range,
        target_drag_x, viewer_gain);
    const float target_y = viewer_target(&angle_y_range,
        target_drag_y, viewer_gain);
    const float target_eye_x = viewer_target(&eye_x_range,
        target_drag_x, viewer_gain);
    const float target_eye_y = viewer_target(&eye_y_range,
        target_drag_y, viewer_gain);
    const float target_z = has_angle_z ? viewer_target(&angle_z_range,
        -target_drag_x * target_drag_y, 1.0f) : 0.0f;
    bongo_cat_live2d_set_centered_dragging(app->live2d, target_drag_x, target_drag_y);
    float previous_x = 0.0f, maximum_step = 0.0f;
    int track_frames[] = {1, 2, 4, 8, 15, 30};
    int return_frames[] = {1, 2, 4, 8, 15, 30};
    passed = bongo_cat_live2d_viewer_timing(app->smoke_viewer_trace,
        track_frames,
        return_frames) && passed;
    passed = parameter(app, "ParamAngleX", &previous_x) && passed;
    int previous = 0;
    float track_x[6] = {0};
    for (size_t i = 0; i < 6; ++i) {
        passed = advance_tracking(app, track_frames[i] - previous,
            &previous_x, &maximum_step) && passed;
        passed = save_timed(app, native, track_names[i], track_frames[i],
            metrics, timing) && passed;
        passed = parameter(app, "ParamAngleX", &track_x[i]) && passed;
        previous = track_frames[i];
    }
    passed = track_x[0] > 0.0f && track_x[0] < target_x * 0.35f &&
        track_x[1] > track_x[0] && track_x[2] > track_x[1] &&
        track_x[3] >= track_x[2] - 0.001f &&
        track_x[4] >= track_x[3] - 0.001f &&
        track_x[5] >= track_x[4] - 0.001f && maximum_step < 6.0f &&
        parameter_near(app, "ParamAngleX", target_x, 0.4f) &&
        parameter_near(app, "ParamAngleY", target_y, 0.4f) &&
        parameter_near(app, "ParamEyeBallX", target_eye_x, 0.02f) &&
        parameter_near(app, "ParamEyeBallY", target_eye_y, 0.02f) && passed;
    if (has_angle_z) passed = parameter_near(app, "ParamAngleZ", target_z, 0.4f) && passed;
    if (has_body_x) passed = parameter_near(app, "ParamBodyAngleX",
        viewer_target(&body_x_range, target_drag_x, viewer_gain), 0.2f) && passed;

    bongo_cat_live2d_set_centered_dragging(app->live2d, 0.0f, 0.0f);
    previous = 0;
    for (size_t i = 0; i < 6; ++i) {
        passed = advance_tracking(app, return_frames[i] - previous, &previous_x, &maximum_step) && passed;
        passed = save_timed(app, native, return_names[i], return_frames[i],
            metrics, timing) && passed;
        previous = return_frames[i];
    }
    passed = maximum_step < 6.0f &&
        parameter_near(app, "ParamAngleX", 0.0f, 0.25f) &&
        parameter_near(app, "ParamAngleY", 0.0f, 0.25f) &&
        parameter_near(app, "ParamEyeBallX", 0.0f, 0.01f) &&
        parameter_near(app, "ParamEyeBallY", 0.0f, 0.01f) &&
        parameter_near(app, "ParamMouseX", 0.0f, 0.001f) &&
        parameter_near(app, "ParamMouseY", 0.0f, 0.001f) && passed;
    if (has_angle_z) passed = parameter_near(app, "ParamAngleZ", 0.0f, 0.25f) && passed;
    fclose(timing);
    fclose(metrics);
    SDL_SetWindowSize(app->window, original_width, original_height);
    SDL_SyncWindow(app->window);
    SDL_GetWindowSizeInPixels(app->window, &audit_width, &audit_height);
    bongo_cat_live2d_resize(app->live2d, audit_width, audit_height);
    app->dirty = true;
    return passed;
}
