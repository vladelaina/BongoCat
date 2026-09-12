#include "runtime.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"
#include "bongo_cat/preferences.h"

#include <stdio.h>
#include <string.h>

static void mark(BongoCatApp *app, const char *name) {
    char path[BONGO_CAT_PATH_CAP];
    bongo_cat_path_join(path, sizeof(path), app->state_root, "runtime-flow-stage.txt");
    FILE *file = bongo_cat_file_open(path, "wb");
    if (file) { fputs(name, file); fclose(file); }
}

static void scale(BongoCatApp *app, float value) {
    bongo_cat_window_cancel_wheel_animation(app);
    bongo_cat_window_set_scale(app, value);
}

static void opacity(BongoCatApp *app, float value) {
    app->session.window.opacity_percent = value;
    bongo_cat_app_cancel_hover_fade(app);
    bongo_cat_platform_set_opacity(&app->platform, value / 100.0f);
}

static void model(BongoCatApp *app, size_t index) {
    if (index < app->models.count) bongo_cat_app_select_model(app, app->models.entries[index].id);
}

static void model_mode(BongoCatApp *app, BongoCatModelMode mode) {
    for (size_t i = 0; i < app->models.count; ++i)
        if (app->models.entries[i].mode == mode) { model(app, i); return; }
}

void bongo_cat_runtime_flow_update(BongoCatApp *app, uint64_t now) {
    if (!app || !app->smoke_runtime_flow) return;
    if (!app->smoke_runtime_flow_ns) {
        app->smoke_runtime_flow_ns = now;
        snprintf(app->smoke_runtime_model, sizeof(app->smoke_runtime_model), "%.*s",
            (int)sizeof(app->smoke_runtime_model) - 1, app->session.active_model_id);
        mark(app, "startup"); return;
    }
    uint64_t elapsed = now - app->smoke_runtime_flow_ns;
    unsigned target = (unsigned)(elapsed / 1500000000ull);
    if (target <= app->smoke_runtime_stage) return;
    app->smoke_runtime_stage = target;
    switch (target) {
    case 1: mark(app, "idle"); break;
    case 2: scale(app, 125.0f); mark(app, "scale-up"); break;
    case 3: scale(app, 75.0f); mark(app, "scale-down"); break;
    case 4: opacity(app, 50.0f); mark(app, "opacity-50"); break;
    case 5: opacity(app, 100.0f); mark(app, "opacity-100"); break;
    case 6: model_mode(app, BONGO_CAT_MODE_KEYBOARD); mark(app, "model-keyboard"); break;
    case 7: model_mode(app, BONGO_CAT_MODE_STANDARD); mark(app, "model-standard"); break;
    case 8: model_mode(app, BONGO_CAT_MODE_GAMEPAD); mark(app, "model-gamepad"); break;
    case 9:
        app->smoke_preference_model_select = true;
        bongo_cat_preferences_show(app->preferences);
        mark(app, "settings-first-model-select"); break;
    case 10: mark(app, "settings-idle"); break;
    case 11:
        bongo_cat_preferences_close(app->preferences);
        model_mode(app, BONGO_CAT_MODE_STANDARD);
        mark(app, "settings-closed-model-restored"); break;
    case 12:
        app->smoke_preference_model_select = true;
        bongo_cat_preferences_show(app->preferences);
        mark(app, "settings-reopen-model-select"); break;
    case 13:
        bongo_cat_preferences_close(app->preferences);
        bongo_cat_app_select_model(app, app->smoke_runtime_model);
        mark(app, "recovery"); break;
    case 14: model_mode(app, BONGO_CAT_MODE_GAMEPAD); mark(app, "gamepad-repeat"); break;
    case 15:
        bongo_cat_app_select_model(app, app->smoke_runtime_model);
        mark(app, "final-recovery"); break;
    default: app->running = false; break;
    }
}
