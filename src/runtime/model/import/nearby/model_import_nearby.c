#include "model_import.h"
#include "model_import_lock.h"
#include "model_import_probe.h"
#include "model_import_mver_policy.h"
#include "model_import_mver.h"
#include "model_import_nearby_internal.h"
#include "../model_import_path.h"
#include "../tauri/model_import_tauri.h"
#include "runtime.h"
#include "bongo_cat/path.h"
#include "bongo_cat/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEARBY_SCAN_BUDGET_NS 500000000ull

static bool existing_identity(const BongoCatModelCatalog *models,
    const char *identity, BongoCatModelMode mode,
    const BongoCatModelEntry *ignored) {
    for (size_t i = 0; models && i < models->count; ++i) {
        const BongoCatModelEntry *entry = &models->entries[i];
        if (entry != ignored && entry->mode == mode &&
            entry->content_digest[0] &&
            strcmp(entry->content_digest, identity) == 0) return true;
    }
    return false;
}

static BongoCatModelEntry *model_with_id(BongoCatApp *app, const char *id) {
    for (size_t i = 0; app && i < app->models.count; ++i)
        if (strcmp(app->models.entries[i].id, id) == 0)
            return &app->models.entries[i];
    return NULL;
}

static const char *candidate_source(const BongoCatImportCandidate *candidate,
    const char *fallback) {
    if (candidate->format == BONGO_CAT_IMPORT_MVER_PATCH &&
        candidate->patch_root[0]) return candidate->patch_root;
    return candidate->package_root[0] ? candidate->package_root : fallback;
}

static void candidate_name(const char *source,
    const BongoCatImportCandidate *candidate, char *name, size_t capacity) {
    const char *value = bongo_cat_path_name(source);
    char parent[BONGO_CAT_PATH_CAP];
    if (value && candidate->format == BONGO_CAT_IMPORT_MVER_PATCH &&
        candidate->package_root[0] &&
        !strcmp(value, bongo_cat_path_name(candidate->package_root)) &&
        bongo_cat_import_parent_path(source, parent, sizeof(parent)))
        value = bongo_cat_path_name(parent);
    snprintf(name, capacity, "%s", value && value[0] ? value : "Nearby model");
}

static bool add_candidate(BongoCatApp *app, const char *cache_root,
    const char *source, const BongoCatImportCandidate *candidate,
    char first_created[BONGO_CAT_ID_CAP], BongoCatError *error) {
    char source_hash[65], signature[65], identity[65];
    char id[BONGO_CAT_ID_CAP], adapter[BONGO_CAT_PATH_CAP];
    bongo_cat_sha256_bytes(source, strlen(source), source_hash);
    snprintf(id, sizeof(id), "nearby-%.16s-%s", source_hash,
        bongo_cat_mode_name(candidate->mode));
    BongoCatModelEntry *same_id = model_with_id(app, id);
    if (same_id && !same_id->managed) return true;
    if (!bongo_cat_nearby_signature(candidate, signature, error) ||
        !bongo_cat_path_join(adapter, sizeof(adapter), cache_root, id))
        return false;
    bool placeholder = false;
    bool identity_cached = bongo_cat_nearby_cached_inspection(adapter, source,
        signature, identity, &placeholder);
    if (!identity_cached &&
        !bongo_cat_import_candidate_inspect(candidate, identity,
            &placeholder, error))
        return false;
    if (placeholder) {
        if (!identity_cached) bongo_cat_nearby_remember_inspection(adapter,
            source, signature, identity, true, candidate->mode);
        return true;
    }
    if (candidate->format == BONGO_CAT_IMPORT_MVER_PATCH) {
        char base_identity[65];
        bool base_placeholder = false;
        if (!bongo_cat_import_patch_base_inspect(candidate, base_identity,
                &base_placeholder, error)) return false;
        if (base_placeholder || existing_identity(&app->models, base_identity,
                candidate->mode, same_id)) return true;
    }
    if (existing_identity(&app->models, identity, candidate->mode, same_id)) {
        if (!identity_cached) bongo_cat_nearby_remember_inspection(adapter,
            source, signature, identity, false, candidate->mode);
        return true;
    }
    if (!same_id && app->models.count >= BONGO_CAT_MODEL_CAP) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Too many models while adding nearby package");
        return false;
    }
    bool created = false;
    if (!bongo_cat_nearby_refresh_cache(candidate, cache_root, id, source,
        signature, identity, false, adapter, &created, error)) return false;
    if (!same_id && existing_identity(&app->models, identity,
        candidate->mode, NULL)) return true;
    BongoCatModelEntry *entry = same_id ? same_id :
        &app->models.entries[app->models.count++];
    memset(entry, 0, sizeof(*entry));
    candidate_name(source, candidate, entry->display_name,
        sizeof(entry->display_name));
    bongo_cat_import_describe_nearby_entry(entry, candidate, id, identity,
        source_hash, source, adapter);
    if (created && !first_created[0])
        snprintf(first_created, BONGO_CAT_ID_CAP, "%s", id);
    return true;
}

typedef struct NearbyAdd {
    BongoCatApp *app;
    const char *cache_root;
    char *first_created;
    bool best_effort;
} NearbyAdd;

static BongoCatResult add_discovery(NearbyAdd *add, const char *fallback,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    for (size_t i = 0; i < discovery->count; ++i) {
        BongoCatImportCandidate *candidate = &discovery->candidates[i];
        const char *source = candidate_source(candidate, fallback);
        if (!add_candidate(add->app, add->cache_root, source, candidate,
                add->first_created, error)) {
            if (!add->best_effort) return error && error->code
                ? error->code : BONGO_CAT_ERROR_IO;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Skipping nearby model: path=%s model=%s error=%s", source,
                candidate->directory, error && error->message[0]
                    ? error->message : "model adaptation failed");
            if (error) *error = (BongoCatError){0};
        }
    }
    return BONGO_CAT_OK;
}

static BongoCatResult add_scanned(void *userdata, const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    return add_discovery(userdata, source, discovery, error);
}

static int discover_direct(const char *root, bool bounded,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    if (bounded) return bongo_cat_import_probe_exact(root, discovery,
        NULL, BONGO_CAT_IMPORT_PROBE_FALLBACK, false, error);
    /* An explicit nearby root uses broad Mver lookup so selecting an inner
       authored folder still resolves to its canonical package root. */
    int found = bongo_cat_import_mver_discover(root, discovery, error);
    if (found <= 0) {
        memset(discovery, 0, sizeof(*discovery));
        if (error) *error = (BongoCatError){0};
        found = bongo_cat_import_mver_patch_discover(root, discovery, error);
    }
    if (found <= 0) {
        memset(discovery, 0, sizeof(*discovery));
        if (error) *error = (BongoCatError){0};
        found = bongo_cat_import_tauri_discover_exact(root,
            discovery, error);
    }
    return found;
}

static BongoCatResult import_root_unlocked(BongoCatApp *app, const char *root,
    bool bounded, BongoCatError *error) {
    if (!app || !root || !app->cache_root[0])
        return BONGO_CAT_ERROR_ARGUMENT;
    if (!bongo_cat_path_is_dir(root)) return BONGO_CAT_OK;
    char cache_root[BONGO_CAT_PATH_CAP];
    char first_created[BONGO_CAT_ID_CAP] = {0};
    if (!bongo_cat_path_join(cache_root, sizeof(cache_root), app->cache_root,
            BONGO_CAT_ADAPTER_CACHE_DIRECTORY) ||
        !bongo_cat_path_create_directory(cache_root))
        return BONGO_CAT_ERROR_IO;
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    if (!discovery) return BONGO_CAT_ERROR_MEMORY;
    int direct = discover_direct(root, bounded, discovery, error);
    NearbyAdd add = {app, cache_root, first_created, bounded};
    BongoCatResult result = direct > 0
        ? add_discovery(&add, root, discovery, error) : BONGO_CAT_OK;
    if (direct <= 0) {
        if (error) *error = (BongoCatError){0};
        result = bounded
            ? bongo_cat_import_scan_budget(root, add_scanned, &add,
                NEARBY_SCAN_BUDGET_NS, error)
            : bongo_cat_import_scan(root, add_scanned, &add, error);
    }
    const char *selected = app->session.active_model_id;
    if (first_created[0] && (!selected[0] || !strcmp(selected, "standard") ||
        !strcmp(selected, "keyboard") || !strcmp(selected, "gamepad")))
        snprintf(app->session.active_model_id,
            sizeof(app->session.active_model_id), "%s", first_created);
    free(discovery);
    return result;
}

static BongoCatResult import_root(BongoCatApp *app, const char *root,
    bool bounded, BongoCatError *error) {
    bongo_cat_import_storage_lock();
    BongoCatResult result = import_root_unlocked(app, root, bounded, error);
    bongo_cat_import_storage_unlock();
    return result;
}

BongoCatResult bongo_cat_import_nearby_root(BongoCatApp *app,
    const char *root, BongoCatError *error) {
    return import_root(app, root, false, error);
}

BongoCatResult bongo_cat_import_nearby_scan(BongoCatApp *app,
    const char *root, BongoCatError *error) {
    return import_root(app, root, true, error);
}

BongoCatResult bongo_cat_import_nearby(BongoCatApp *app,
    const char *root, BongoCatError *error) {
    if (!app || !root) return BONGO_CAT_ERROR_ARGUMENT;
    size_t before = app->models.count;
    BongoCatResult result = bongo_cat_import_nearby_root(app, root, error);
    if (result != BONGO_CAT_OK || app->models.count != before) return result;
    char parent[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_import_parent_path(root, parent, sizeof(parent)) ||
        !strcmp(parent, root))
        return BONGO_CAT_OK;
    if (error) *error = (BongoCatError){0};
    return bongo_cat_import_nearby_root(app, parent, error);
}
