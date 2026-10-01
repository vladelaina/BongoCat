#include "runtime.h"
#include "runtime_state.h"
#include "bongo_cat/file.h"
#include "bongo_cat/log.h"
#include "bongo_cat/path.h"
#include "storage_paths.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "windows_package.h"
#endif

static char runtime_log_path[BONGO_CAT_PATH_CAP];
static char log_source[BONGO_CAT_ID_CAP + 8] = "unknown";
static bool startup_is_ready;
static SDL_Mutex *log_mutex;

static const char *priority_name(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_TRACE: return "TRACE";
    case SDL_LOG_PRIORITY_VERBOSE: return "VERBOSE";
    case SDL_LOG_PRIORITY_DEBUG: return "DEBUG";
    case SDL_LOG_PRIORITY_INFO: return "INFO";
    case SDL_LOG_PRIORITY_WARN: return "WARN";
    case SDL_LOG_PRIORITY_ERROR: return "ERROR";
    case SDL_LOG_PRIORITY_CRITICAL: return "CRITICAL";
    default: return "UNKNOWN";
    }
}

static void append_log(const char *path, const char *timestamp,
    int category, SDL_LogPriority priority, const char *message) {
    if (!path || !path[0]) return;
    const char *text = message ? message : "";
    int prefix = snprintf(NULL, 0, "%s [%s] [%s:%d] ",
        timestamp ? timestamp : "unknown-time", log_source,
        priority_name(priority), category);
    size_t text_length = strlen(text);
    char *line = prefix >= 0 && text_length < SIZE_MAX - (size_t)prefix - 2
        ? malloc((size_t)prefix + text_length + 2) : NULL;
    if (!line) return;
    snprintf(line, (size_t)prefix + 1, "%s [%s] [%s:%d] ",
        timestamp ? timestamp : "unknown-time", log_source,
        priority_name(priority), category);
    for (size_t i = 0; i < text_length; ++i)
        line[(size_t)prefix + i] = text[i] == '\r' || text[i] == '\n'
            ? ' ' : text[i];
    size_t length = (size_t)prefix + text_length;
    line[length++] = '\n';
    bongo_cat_file_append(path, line, length);
    free(line);
}

static void SDLCALL log_output(void *userdata, int category,
    SDL_LogPriority priority, const char *message) {
    (void)userdata;
    if (!bongo_cat_log_enabled(category, priority)) return;
    if (log_mutex) SDL_LockMutex(log_mutex);
    char timestamp[64];
    bongo_cat_runtime_timestamp(timestamp, sizeof(timestamp));
    append_log(runtime_log_path, timestamp, category, priority, message);
    fprintf(stderr, "%s [%s] [%s:%d] %s\n", timestamp, log_source,
        priority_name(priority), category, message ? message : "");
    if (log_mutex) SDL_UnlockMutex(log_mutex);
}

void bongo_cat_runtime_clean_shutdown(BongoCatApp *app, int exit_code) {
    char timestamp[64], message[80];
    bongo_cat_runtime_timestamp(timestamp, sizeof(timestamp));
    snprintf(message, sizeof(message),
        "[runtime] Shutdown complete: exit_code=%d", exit_code);
    append_log(runtime_log_path, timestamp, BONGO_CAT_LOG_LIFECYCLE,
        SDL_LOG_PRIORITY_INFO, message);
    fprintf(stderr, "%s [%s] [%s:%d] %s\n", timestamp, log_source,
        priority_name(SDL_LOG_PRIORITY_INFO),
        BONGO_CAT_LOG_LIFECYCLE, message);

    bongo_cat_runtime_state_clean(app, timestamp);
}

void bongo_cat_runtime_log_stop(void) {
    if (!log_mutex) return;
    SDL_Mutex *mutex = log_mutex;
    log_mutex = NULL;
    SDL_DestroyMutex(mutex);
}

static void set_log_source(const BongoCatApp *app) {
    if (app && app->secondary_pet)
        snprintf(log_source, sizeof(log_source), "pet:%s",
            app->secondary_model_id);
    else snprintf(log_source, sizeof(log_source), "primary");
    for (size_t i = 0; log_source[i]; ++i)
        if (log_source[i] == '\r' || log_source[i] == '\n')
            log_source[i] = ' ';
}

static void begin_log(BongoCatApp *app) {
    char stage_path[BONGO_CAT_PATH_CAP];
    char stage_temporary[BONGO_CAT_PATH_CAP];
    char interrupted[128] = {0};
    char unexpected[256] = {0};
    bongo_cat_runtime_state_previous(app, unexpected, sizeof(unexpected));
    runtime_log_path[0] = '\0';
    bongo_cat_path_join(runtime_log_path, sizeof(runtime_log_path),
        app->log_root, "BongoCat.log");
    /* The primary process owns the session log. Secondary pets append to it
       instead of erasing diagnostics already written by the primary. */
    if (runtime_log_path[0] && !app->secondary_pet) {
        FILE *fresh_log = bongo_cat_file_open(runtime_log_path, "wb");
        if (fresh_log) fclose(fresh_log);
    }
    set_log_source(app);
    bool stage_paths_ready = bongo_cat_path_join(stage_path,
        sizeof(stage_path), app->state_root, "startup-stage.txt") &&
        bongo_cat_path_join(stage_temporary, sizeof(stage_temporary),
            app->state_root, "startup-stage.tmp");
    if (stage_paths_ready && !bongo_cat_path_is_file(stage_path) &&
        bongo_cat_path_is_file(stage_temporary))
        bongo_cat_file_replace(stage_temporary, stage_path, true);
    else if (stage_paths_ready) bongo_cat_file_remove(stage_temporary);
    FILE *stage = stage_paths_ready ? bongo_cat_file_open(stage_path, "rb")
        : NULL;
    if (stage) {
        size_t length = fread(interrupted, 1, sizeof(interrupted) - 1, stage);
        interrupted[ferror(stage) ? 0 : length] = '\0';
        fclose(stage);
    }
    if (!log_mutex) log_mutex = SDL_CreateMutex();
    SDL_SetLogOutputFunction(log_output, NULL);
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_WARN);
    SDL_SetLogPriority(BONGO_CAT_LOG_LIFECYCLE, SDL_LOG_PRIORITY_INFO);
    SDL_SetLogPriority(BONGO_CAT_LOG_UPDATE, SDL_LOG_PRIORITY_INFO);
    SDL_SetLogPriority(BONGO_CAT_LOG_INPUT, SDL_LOG_PRIORITY_INFO);
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[runtime] Process started: version=%s platform=%s",
        BONGO_CAT_VERSION, SDL_GetPlatform());
#ifdef _WIN32
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE, "[runtime] Package identity: %s",
        bongo_cat_windows_is_packaged() ? "MSIX" : "unpackaged Win32");
#endif
    if (interrupted[0]) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Previous startup ended before readiness at stage: %s", interrupted);
    if (unexpected[0] && !strstr(unexpected, "stage=clean-shutdown"))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Previous run ended unexpectedly (crash, forced close, or hang): %s",
            unexpected);
}

bool bongo_cat_startup_prepare(BongoCatApp *app, int argc, char **argv,
    BongoCatError *error) {
    if (!bongo_cat_startup_arguments(app, argc, argv, error) ||
        !bongo_cat_storage_paths_prepare(app, error)) return false;
    begin_log(app); bongo_cat_startup_stage(app, "paths-ready"); return true;
}

void bongo_cat_startup_stage(BongoCatApp *app, const char *stage) {
    if (!app || !app->state_root[0] || !stage) return;
    char path[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    if (bongo_cat_path_join(path, sizeof(path), app->state_root,
        "startup-stage.txt") && bongo_cat_path_join(temporary,
        sizeof(temporary), app->state_root, "startup-stage.tmp")) {
        FILE *file = bongo_cat_file_open(temporary, "wb");
        bool written = file && fputs(stage, file) >= 0;
        if (file && fclose(file) != 0) written = false;
        if (!written || !bongo_cat_file_replace(temporary, path, true))
            bongo_cat_file_remove(temporary);
    }
    char runtime_stage[160];
    snprintf(runtime_stage, sizeof(runtime_stage), "startup:%s", stage);
    bongo_cat_runtime_stage(app, runtime_stage);
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE, "[runtime] Startup stage: %s", stage);
}

void bongo_cat_startup_ready(BongoCatApp *app) {
    if (!app || startup_is_ready) return;
    startup_is_ready = true;
    char path[BONGO_CAT_PATH_CAP];
    if (bongo_cat_path_join(path, sizeof(path), app->state_root,
        "startup-stage.txt")) bongo_cat_file_remove(path);
    if (bongo_cat_path_join(path, sizeof(path), app->state_root,
        "startup-stage.tmp")) bongo_cat_file_remove(path);
    bongo_cat_runtime_stage(app, "running");
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE, "[runtime] Startup ready");
}

void bongo_cat_startup_failure(BongoCatApp *app, const BongoCatError *error) {
    const char *message = error && error->message[0] ? error->message : "Initialization failed";
    if (app) bongo_cat_startup_stage(app, "failed");
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Startup failed: %s", message);
}

void bongo_cat_startup_ci_failure(BongoCatApp *app,
    const BongoCatError *error) {
    if (!app) return;
    app->exit_code = 1;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[runtime] CI failure: %s",
        error && error->message[0] ? error->message : "CI operation failed");
}
