#include "runtime.h"
#include "model_cover_paths.h"
#include "preferences_model_cover.h"
#include "preferences_notice.h"
#include "bongo_cat/file.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CONTROL_FILE "control.txt"
#define CONTROL_HEARTBEAT_NS 2000000000ull
#define CHILD_RETRY_NS 2000000000ull
#define CHILD_STOP_NS 1000000000ull
typedef struct SecondaryPet {
    char model_id[BONGO_CAT_ID_CAP];
    SDL_Process *process;
    uint64_t retry_ns, stop_ns;
    uint64_t cover_size, cover_modified;
    unsigned failures;
    bool stopping, control_ready, control_visible, control_pass_through;
} SecondaryPet;
struct BongoCatMultiPetRuntime {
    SecondaryPet pets[BONGO_CAT_ADDITIONAL_MODEL_CAP];
    size_t count;
    uint64_t heartbeat_ns;
};
static bool control_path(char *target, size_t capacity,
    const BongoCatApp *app, const char *model_id, bool create) {
    char pets[BONGO_CAT_PATH_CAP], directory[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(pets, sizeof(pets), app->primary_state_root,
            "pets") || (create && !bongo_cat_path_create_directory(pets)) ||
        !bongo_cat_multi_pet_state_directory(directory, sizeof(directory),
            app->primary_state_root, model_id) ||
        (create && !bongo_cat_path_create_directory(directory))) return false;
    return bongo_cat_path_join(target, capacity, directory, CONTROL_FILE);
}

static bool write_control(BongoCatApp *app, SecondaryPet *pet) {
    char path[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    int length;
    if (!control_path(path, sizeof(path), app, pet->model_id, true) ||
        (length = snprintf(temporary, sizeof(temporary), "%s.tmp", path)) < 0 ||
        (size_t)length >= sizeof(temporary)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet control path preparation failed: model=%s",
            pet->model_id);
        return false;
    }
    FILE *file = bongo_cat_file_open(temporary, "wb");
    bool written = file && fprintf(file, "%s %lld %d\n",
        app->session.window.visible ? "visible" : "hidden",
        (long long)time(NULL), app->settings.window.pass_through) > 0;
    if (file && fclose(file) != 0) written = false;
    if (!written || !bongo_cat_file_replace(temporary, path, true)) {
        bongo_cat_file_remove(temporary);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "[runtime] Multi-pet control write failed: model=%s path=%s "
            "visible=%d pass_through=%d", pet->model_id, path,
            app->session.window.visible, app->settings.window.pass_through);
        return false;
    }
    pet->control_ready = true;
    pet->control_visible = app->session.window.visible;
    pet->control_pass_through = app->settings.window.pass_through;
    return true;
}

static void remove_control(BongoCatApp *app, SecondaryPet *pet) {
    char path[BONGO_CAT_PATH_CAP];
    if (control_path(path, sizeof(path), app, pet->model_id, false))
        bongo_cat_file_remove(path);
    pet->control_ready = false;
}

static SecondaryPet *find_pet(BongoCatMultiPetRuntime *runtime,
    const char *model_id) {
    for (size_t i = 0; i < runtime->count; ++i)
        if (!strcmp(runtime->pets[i].model_id, model_id))
            return &runtime->pets[i];
    return NULL;
}

static void remove_pet(BongoCatMultiPetRuntime *runtime, size_t index) {
    if (index + 1 < runtime->count)
        memmove(&runtime->pets[index], &runtime->pets[index + 1],
            (runtime->count - index - 1) * sizeof(runtime->pets[0]));
    memset(&runtime->pets[--runtime->count], 0, sizeof(runtime->pets[0]));
}

static bool desired(const BongoCatApp *app, const char *model_id) {
    if (!app->settings.model.multiple_pets) return false;
    for (size_t i = 0; i < app->session.additional_model_count; ++i)
        if (!strcmp(app->session.additional_model_ids[i], model_id))
            return true;
    return false;
}

static void refresh_pet_cover(BongoCatApp *app, SecondaryPet *pet) {
    if (!app->preferences || !SDL_GL_GetCurrentContext()) return;
    const BongoCatModelEntry *entry = bongo_cat_models_find(&app->models,
        pet->model_id);
    char path[BONGO_CAT_PATH_CAP];
    uint64_t size, modified;
    if (!entry || !bongo_cat_path_join(path, sizeof(path),
            entry->adapter_directory, BONGO_CAT_MODEL_COVER_FILE) ||
        !bongo_cat_path_file_info(path, &size, &modified) ||
        (pet->cover_size == size && pet->cover_modified == modified)) return;
    /* The child writes the shared PNG, but only the primary owns the UI
       texture cache. Retry on the next heartbeat if reloading fails. */
    if (bongo_cat_preferences_model_cover_reload(app, path)) {
        pet->cover_size = size;
        pet->cover_modified = modified;
    }
}

static SDL_Process *create_pet_process(const char *const *args) {
    SDL_PropertiesID properties = SDL_CreateProperties();
    if (!properties) return NULL;
    bool configured = SDL_SetPointerProperty(properties,
            SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void *)args) &&
        SDL_SetNumberProperty(properties,
            SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
            SDL_PROCESS_STDIO_NULL) &&
        SDL_SetNumberProperty(properties,
            SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
            SDL_PROCESS_STDIO_NULL) &&
        SDL_SetNumberProperty(properties,
            SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
            SDL_PROCESS_STDIO_NULL);
    SDL_Process *process = configured ?
        SDL_CreateProcessWithProperties(properties) : NULL;
    if (!configured) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "[runtime] Multi-pet process properties failed: %s", SDL_GetError());
    SDL_DestroyProperties(properties);
    return process;
}

static bool spawn_pet(BongoCatApp *app, SecondaryPet *pet, size_t index,
    uint64_t now) {
    char model_arg[BONGO_CAT_ID_CAP + 20];
    char storage_arg[BONGO_CAT_PATH_CAP + 20];
    char nearby_arg[BONGO_CAT_PATH_CAP + 20];
    char position_arg[80];
    int x = app->session.window.x, y = app->session.window.y;
    if (app->window) SDL_GetWindowPosition(app->window, &x, &y);
    snprintf(model_arg, sizeof(model_arg), "--secondary-pet=%s", pet->model_id);
    snprintf(position_arg, sizeof(position_arg), "--secondary-position=%d,%d",
        x + 36 * (int)(index + 1), y + 36 * (int)(index + 1));
    const char *args[6] = {app->executable_path[0] ? app->executable_path :
        BONGO_CAT_EXECUTABLE, model_arg, position_arg, NULL, NULL, NULL};
    size_t argument = 3;
    if (app->storage_root[0]) {
        snprintf(storage_arg, sizeof(storage_arg), "--storage-root=%s",
            app->storage_root);
        args[argument++] = storage_arg;
    }
    if (app->nearby_root[0]) {
        snprintf(nearby_arg, sizeof(nearby_arg), "--nearby-root=%s",
            app->nearby_root);
        args[argument++] = nearby_arg;
    }
    bongo_cat_multi_pet_clear_remove_request(app, pet->model_id);
    bongo_cat_multi_pet_clear_pass_through_request(app, pet->model_id);
    bongo_cat_multi_pet_clear_primary_request(app, pet->model_id);
    if (!write_control(app, pet)) {
        pet->failures++;
        pet->retry_ns = now + CHILD_RETRY_NS;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Unable to prepare additional desktop pet %s",
            pet->model_id);
        return false;
    }
    /* GUI launches may not have inheritable standard handles on Windows. */
    pet->process = create_pet_process(args);
    if (pet->process) {
        pet->stopping = false;
        return true;
    }
    pet->failures++;
    pet->retry_ns = now + CHILD_RETRY_NS;
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "Unable to start additional desktop pet %s: %s",
        pet->model_id, SDL_GetError());
    return false;
}

static void reap_pets(BongoCatApp *app, uint64_t now) {
    BongoCatMultiPetRuntime *runtime = app->multi_pet;
    for (size_t i = runtime->count; i > 0; --i) {
        SecondaryPet *pet = &runtime->pets[i - 1];
        int exit_code = -1;
        bool exited = pet->process &&
            SDL_WaitProcess(pet->process, false, &exit_code);
        if (exited) {
            if (!pet->stopping)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[runtime] Multi-pet process exited unexpectedly: "
                    "model=%s exit_code=%d", pet->model_id, exit_code);
            SDL_DestroyProcess(pet->process);
            pet->process = NULL;
            if (!pet->stopping) {
                pet->failures++;
                pet->retry_ns = now + CHILD_RETRY_NS;
            }
        }
        if (pet->stopping && pet->process && now >= pet->stop_ns) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[runtime] Multi-pet process stop timed out; killing: model=%s",
                pet->model_id);
            SDL_KillProcess(pet->process, true);
            SDL_WaitProcess(pet->process, true, NULL);
            SDL_DestroyProcess(pet->process);
            pet->process = NULL;
        }
        if (pet->stopping && !pet->process) remove_pet(runtime, i - 1);
    }
}

static void consume_remove_requests(BongoCatApp *app) {
    BongoCatMultiPetRuntime *runtime = app->multi_pet;
    for (size_t i = runtime->count; i > 0; --i) {
        SecondaryPet *pet = &runtime->pets[i - 1];
        if (!pet->process || pet->stopping || !desired(app, pet->model_id) ||
            !bongo_cat_multi_pet_take_remove_request(app, pet->model_id))
            continue;
        bongo_cat_session_remove_model(&app->session, pet->model_id);
        bongo_cat_preferences_invalidate(app->preferences);
    }
}

void bongo_cat_multi_pet_primary_update(BongoCatApp *app, uint64_t now) {
    bongo_cat_multi_pet_prune_selection(app);
    if (!app->multi_pet && app->session.additional_model_count)
        app->multi_pet = calloc(1, sizeof(*app->multi_pet));
    if (!app->multi_pet) return;
    BongoCatMultiPetRuntime *runtime = app->multi_pet;
    consume_remove_requests(app);
    bongo_cat_multi_pet_pass_through_requests_update(app);
    bongo_cat_multi_pet_primary_requests_update(app);
    reap_pets(app, now);
    for (size_t i = 0; i < runtime->count; ++i) {
        SecondaryPet *pet = &runtime->pets[i];
        if (!desired(app, pet->model_id) && !pet->stopping) {
            remove_control(app, pet);
            pet->stopping = true;
            pet->stop_ns = now + CHILD_STOP_NS;
        }
    }
    bool heartbeat = !runtime->heartbeat_ns ||
        now - runtime->heartbeat_ns >= CONTROL_HEARTBEAT_NS;
    for (size_t i = 0; i < app->session.additional_model_count; ++i) {
        const char *id = app->session.additional_model_ids[i];
        if (!desired(app, id)) continue;
        SecondaryPet *pet = find_pet(runtime, id);
        if (!pet && runtime->count < BONGO_CAT_ADDITIONAL_MODEL_CAP) {
            pet = &runtime->pets[runtime->count++];
            snprintf(pet->model_id, sizeof(pet->model_id), "%s", id);
        }
        if (!pet || pet->stopping) continue;
        if (heartbeat) refresh_pet_cover(app, pet);
        if (heartbeat || !pet->control_ready ||
            pet->control_visible != app->session.window.visible ||
            pet->control_pass_through != app->settings.window.pass_through)
            write_control(app, pet);
        if (!pet->process && pet->failures < 3 && now >= pet->retry_ns)
            spawn_pet(app, pet, i, now);
        if (!pet->process && pet->failures >= 3) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "[runtime] Multi-pet model removed after launch failures: "
                "model=%s failures=%u", pet->model_id, pet->failures);
            bongo_cat_session_remove_model(&app->session, pet->model_id);
            remove_control(app, pet);
            pet->stopping = true;
            if (app->preferences) bongo_cat_preferences_notice_show(app,
                bongo_cat_i18n_get(app->i18n, "native.modelLoadFailed",
                    "Unable to display this model"), true);
        }
    }
    if (heartbeat) runtime->heartbeat_ns = now;
}

void bongo_cat_multi_pet_shutdown(BongoCatApp *app) {
    if (!app || !app->multi_pet) return;
    BongoCatMultiPetRuntime *runtime = app->multi_pet;
    for (size_t i = 0; i < runtime->count; ++i)
        remove_control(app, &runtime->pets[i]);
    uint64_t deadline = SDL_GetTicksNS() + 500000000ull;
    for (size_t i = 0; i < runtime->count; ++i) {
        SecondaryPet *pet = &runtime->pets[i];
        while (pet->process && SDL_GetTicksNS() < deadline &&
            !SDL_WaitProcess(pet->process, false, NULL)) SDL_Delay(10);
        if (pet->process && !SDL_WaitProcess(pet->process, false, NULL)) {
            SDL_KillProcess(pet->process, true);
            SDL_WaitProcess(pet->process, true, NULL);
        }
        if (pet->process) SDL_DestroyProcess(pet->process);
    }
    free(runtime);
    app->multi_pet = NULL;
}
