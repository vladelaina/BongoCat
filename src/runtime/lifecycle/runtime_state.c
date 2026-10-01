#include "runtime.h"
#include "runtime_state.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <time.h>

static bool state_path(BongoCatApp *app, char *path, size_t capacity) {
    return app && app->state_root[0] &&
        bongo_cat_path_join(path, capacity, app->state_root,
            "runtime-state.txt");
}

static bool temporary_path(BongoCatApp *app, char *path, size_t capacity) {
    return app && app->state_root[0] &&
        bongo_cat_path_join(path, capacity, app->state_root,
            "runtime-state.tmp");
}

void bongo_cat_runtime_timestamp(char *target, size_t capacity) {
    if (!target || !capacity) return;
    SDL_Time ticks = 0;
    SDL_DateTime date = {0};
    if (SDL_GetCurrentTime(&ticks) &&
        SDL_TimeToDateTime(ticks, &date, true)) {
        int offset_minutes = date.utc_offset >= 0
            ? (date.utc_offset + 30) / 60
            : -((-date.utc_offset + 30) / 60);
        char sign = offset_minutes < 0 ? '-' : '+';
        if (offset_minutes < 0) offset_minutes = -offset_minutes;
        snprintf(target, capacity,
            "%04d-%02d-%02d %02d:%02d:%02d.%03d %c%02d:%02d",
            date.year, date.month, date.day, date.hour, date.minute,
            date.second, date.nanosecond / 1000000, sign,
            offset_minutes / 60, offset_minutes % 60);
    } else {
        snprintf(target, capacity, "%lld", (long long)time(NULL));
    }
    target[capacity - 1] = '\0';
}

void bongo_cat_runtime_state_previous(BongoCatApp *app, char *state,
    size_t capacity) {
    if (!state || !capacity) return;
    state[0] = '\0';
    char path[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    if (!state_path(app, path, sizeof(path)) ||
        !temporary_path(app, temporary, sizeof(temporary))) return;
    if (!bongo_cat_path_is_file(path) &&
        bongo_cat_path_is_file(temporary))
        bongo_cat_file_replace(temporary, path, true);
    else bongo_cat_file_remove(temporary);
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return;
    size_t length = fread(state, 1, capacity - 1, file);
    state[ferror(file) ? 0 : length] = '\0';
    fclose(file);
    for (size_t i = 0; state[i]; ++i)
        if (state[i] == '\r' || state[i] == '\n') state[i] = ' ';
}

static bool write_state(BongoCatApp *app, const char *stage,
    const char *timestamp) {
    char path[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    if (!stage || !timestamp || !state_path(app, path, sizeof(path)) ||
        !temporary_path(app, temporary, sizeof(temporary))) return false;
    FILE *file = bongo_cat_file_open(temporary, "wb");
    if (!file) return false;
    bool written = fprintf(file, "time=%s stage=", timestamp) >= 0;
    for (const char *cursor = stage; *cursor; ++cursor)
        if (fputc(*cursor == '\r' || *cursor == '\n' ? ' ' : *cursor,
            file) == EOF) written = false;
    if (fputc('\n', file) == EOF) written = false;
    if (fclose(file) != 0) written = false;
    if (written && bongo_cat_file_replace(temporary, path, true)) return true;
    bongo_cat_file_remove(temporary);
    return false;
}

void bongo_cat_runtime_stage(BongoCatApp *app, const char *stage) {
    char timestamp[64];
    bongo_cat_runtime_timestamp(timestamp, sizeof(timestamp));
    write_state(app, stage, timestamp);
}

void bongo_cat_runtime_state_clean(BongoCatApp *app, const char *timestamp) {
    char path[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    if (temporary_path(app, temporary, sizeof(temporary)))
        bongo_cat_file_remove(temporary);
    if (state_path(app, path, sizeof(path)) &&
        !bongo_cat_file_remove(path) && bongo_cat_path_is_file(path))
        write_state(app, "clean-shutdown", timestamp);
}
