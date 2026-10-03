#include "model_import.h"
#include "model_import_archive.h"
#include "model_import_lock.h"
#include "model_import_session_internal.h"
#include "model_storage.h"
#include "bongo_cat/path.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BongoCatImportSession *session_create_unlocked(const char *models_root,
    BongoCatError *error) {
    uint64_t started = SDL_GetTicksNS();
    SDL_Log("[runtime] Model import session creation started: root=%s",
        models_root);
    BongoCatImportSession *session = calloc(1, sizeof(*session));
    if (!session) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model import session");
        return NULL;
    }
    session->digests = bongo_cat_import_digest_cache_create();
    int root_length = snprintf(session->root, sizeof(session->root), "%s",
        models_root);
    if (!session->digests || root_length < 0 ||
        (size_t)root_length >= sizeof(session->root) ||
        !bongo_cat_path_create_directory(session->root)) {
        bongo_cat_import_session_destroy(session);
        return NULL;
    }
    SDL_Log("[runtime] Model import session creation: stage=cleanup root=%s",
        session->root);
    if (!bongo_cat_model_cleanup_imports(session->root, error)) {
        bongo_cat_import_session_destroy(session);
        return NULL;
    }
    SDL_Log("[runtime] Model import session creation: stage=index root=%s",
        session->root);
    session->packages = bongo_cat_import_package_index_create(session->root,
        session->digests, error);
    if (!session->packages) {
        bongo_cat_import_session_destroy(session);
        return NULL;
    }
    SDL_Log("[runtime] Model import session creation completed: "
        "elapsed_ms=%.1f root=%s",
        (SDL_GetTicksNS() - started) / 1000000.0, session->root);
    return session;
}

BongoCatImportSession *bongo_cat_import_session_create(const char *models_root,
    BongoCatError *error) {
    if (!models_root) return NULL;
    SDL_Log("[runtime] Model import session lock: stage=waiting");
    bongo_cat_import_storage_lock();
    SDL_Log("[runtime] Model import session lock: stage=acquired");
    BongoCatImportSession *session = session_create_unlocked(models_root,
        error);
    bongo_cat_import_storage_unlock();
    SDL_Log("[runtime] Model import session lock: stage=released result=%d",
        session != NULL);
    return session;
}

void bongo_cat_import_session_destroy(BongoCatImportSession *session) {
    if (!session) return;
    bongo_cat_import_package_index_destroy(session->packages);
    bongo_cat_import_digest_cache_destroy(session->digests);
    free(session);
}

static BongoCatResult session_install_unlocked(
    BongoCatImportSession *session, const char *source,
    BongoCatImportReceipt *receipt, BongoCatError *error) {
    char source_directory[BONGO_CAT_PATH_CAP];
    uint64_t started = SDL_GetTicksNS();
    BongoCatResult source_result = bongo_cat_import_source_directory(source,
        source_directory, sizeof(source_directory), error);
    if (source_result != BONGO_CAT_OK) return source_result;
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    BongoCatPackageMetadata *metadata = calloc(BONGO_CAT_IMPORT_CANDIDATE_CAP,
        sizeof(*metadata));
    if (!discovery || !metadata) {
        free(discovery);
        free(metadata);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model import workspace");
        return BONGO_CAT_ERROR_MEMORY;
    }
    SDL_Log("[runtime] Model import package discovery started: path=%s",
        source_directory);
    bool discovered = bongo_cat_import_discover(source_directory, discovery,
        error);
    SDL_Log("[runtime] Model import package discovery completed: result=%d "
        "candidates=%llu elapsed_ms=%.1f path=%s", discovered,
        (unsigned long long)discovery->count,
        (SDL_GetTicksNS() - started) / 1000000.0, source_directory);
    bool metadata_ready = discovered &&
        bongo_cat_import_prepare_package_metadata_cached(discovery, metadata,
            session->digests, error);
    SDL_Log("[runtime] Model import package fingerprint completed: result=%d "
        "candidates=%llu elapsed_ms=%.1f path=%s", metadata_ready,
        (unsigned long long)discovery->count,
        (SDL_GetTicksNS() - started) / 1000000.0, source_directory);
    if (!metadata_ready) {
        BongoCatResult result = error && error->code ? error->code :
            BONGO_CAT_ERROR_FORMAT;
        free(discovery);
        free(metadata);
        return result;
    }
    BongoCatResult result = bongo_cat_import_install_discovery(session,
        discovery, metadata, receipt, error);
    SDL_Log("[runtime] Model import package completed: result=%d variants=%llu "
        "installed=%llu elapsed_ms=%.1f path=%s", (int)result,
        (unsigned long long)(receipt ? receipt->count : 0),
        (unsigned long long)(receipt ? receipt->installed_count : 0),
        (SDL_GetTicksNS() - started) / 1000000.0, source_directory);
    free(discovery);
    free(metadata);
    return result;
}

BongoCatResult bongo_cat_import_session_install(
    BongoCatImportSession *session, const char *source,
    BongoCatImportReceipt *receipt, BongoCatError *error) {
    if (receipt) memset(receipt, 0, sizeof(*receipt));
    if (!session || !source) return BONGO_CAT_ERROR_ARGUMENT;
    SDL_Log("[runtime] Model import storage lock: stage=waiting path=%s",
        source);
    bongo_cat_import_storage_lock();
    SDL_Log("[runtime] Model import storage lock: stage=acquired path=%s",
        source);
    char directory[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP] = "";
    BongoCatResult result = BONGO_CAT_OK;
    bool archive = bongo_cat_import_is_archive(source);
    if (archive) result = bongo_cat_import_archive_extract(source, directory,
        temporary, error);
    if (result == BONGO_CAT_OK)
        result = session_install_unlocked(session, archive ? directory : source,
            receipt, error);
    bongo_cat_import_archive_cleanup(temporary);
    bongo_cat_import_storage_unlock();
    SDL_Log("[runtime] Model import storage lock: stage=released result=%d "
        "path=%s", (int)result, source);
    return result;
}

BongoCatResult bongo_cat_import_install(const char *source,
    const char *models_root, BongoCatImportReceipt *receipt,
    BongoCatError *error) {
    if (receipt) memset(receipt, 0, sizeof(*receipt));
    if (!source || !models_root) return BONGO_CAT_ERROR_ARGUMENT;
    BongoCatImportSession *session = bongo_cat_import_session_create(
        models_root, error);
    if (!session) return error && error->code ? error->code :
        BONGO_CAT_ERROR_IO;
    BongoCatResult result = bongo_cat_import_session_install(session,
        source, receipt, error);
    bongo_cat_import_session_destroy(session);
    return result;
}
