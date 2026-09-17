#include "model_import.h"
#include "model_import_archive.h"
#include "model_import_probe.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ProgressiveBase {
    char directory[BONGO_CAT_PATH_CAP];
    char setting[BONGO_CAT_PATH_CAP];
    BongoCatModelMode mode;
} ProgressiveBase;

typedef struct ProgressiveUnit {
    char source[BONGO_CAT_PATH_CAP];
    bool patch_only;
} ProgressiveUnit;

typedef struct ProgressiveScan {
    ProgressiveUnit units[BONGO_CAT_MODEL_CAP];
    ProgressiveBase bases[BONGO_CAT_MODEL_CAP];
    size_t unit_count;
    size_t base_count;
    size_t model_count;
} ProgressiveScan;

static BongoCatResult collect_unit(void *userdata, const char *source,
    BongoCatImportDiscovery *discovery, BongoCatError *error) {
    ProgressiveScan *scan = userdata;
    if (scan->unit_count >= BONGO_CAT_MODEL_CAP ||
        discovery->count > BONGO_CAT_MODEL_CAP - scan->model_count) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Selected folder contains too many model variants");
        return BONGO_CAT_ERROR_FORMAT;
    }
    ProgressiveUnit *unit = &scan->units[scan->unit_count++];
    snprintf(unit->source, sizeof(unit->source), "%s", source);
    unit->patch_only = discovery->count > 0;
    scan->model_count += discovery->count;
    for (size_t i = 0; i < discovery->count; ++i) {
        const BongoCatImportCandidate *candidate = &discovery->candidates[i];
        if (candidate->format != BONGO_CAT_IMPORT_MVER_PATCH)
            unit->patch_only = false;
        if (candidate->format != BONGO_CAT_IMPORT_MVER) continue;
        if (scan->base_count >= BONGO_CAT_MODEL_CAP) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
                "Selected folder contains too many model variants");
            return BONGO_CAT_ERROR_FORMAT;
        }
        ProgressiveBase *base = &scan->bases[scan->base_count++];
        snprintf(base->directory, sizeof(base->directory), "%s",
            candidate->directory);
        snprintf(base->setting, sizeof(base->setting), "%s",
            candidate->setting);
        base->mode = candidate->mode;
    }
    return BONGO_CAT_OK;
}

static bool has_base(const ProgressiveScan *scan,
    const BongoCatImportCandidate *patch) {
    for (size_t i = 0; i < scan->base_count; ++i) {
        const ProgressiveBase *base = &scan->bases[i];
        if (base->mode == patch->mode &&
            strcmp(base->directory, patch->directory) == 0 &&
            strcmp(base->setting, patch->setting) == 0) return true;
    }
    return false;
}

static bool redundant_patch(const ProgressiveScan *scan,
    const ProgressiveUnit *unit) {
    if (!unit->patch_only) return false;
    BongoCatImportDiscovery *discovery = calloc(1, sizeof(*discovery));
    if (!discovery) return false;
    BongoCatError ignored = {0};
    bool redundant = bongo_cat_import_probe_exact(unit->source, discovery,
        NULL, BONGO_CAT_IMPORT_PROBE_STRICT, false, &ignored) > 0 &&
        discovery->count > 0;
    for (size_t i = 0; redundant && i < discovery->count; ++i)
        if (!has_base(scan, &discovery->candidates[i])) redundant = false;
    free(discovery);
    return redundant;
}

static void record_failure_name(BongoCatImportBatchStats *stats,
    const char *source) {
    if (!stats || stats->failure_name_count >=
        BONGO_CAT_IMPORT_FAILURE_NAME_CAP) return;
    const char *name = bongo_cat_path_name(source);
    if (!name || !name[0]) return;
    snprintf(stats->failure_names[stats->failure_name_count++],
        BONGO_CAT_ID_CAP, "%s", name);
}

static BongoCatResult install_one(BongoCatImportSession *session,
    const char *source, BongoCatImportReceiptCallback callback,
    void *userdata, BongoCatImportBatchStats *stats, BongoCatError *error) {
    BongoCatImportReceipt receipt = {0};
    uint64_t started = SDL_GetTicksNS();
    SDL_Log("[runtime] Model import unit started: path=%s", source);
    BongoCatResult result = bongo_cat_import_session_install(session, source,
        &receipt, error);
    if (result == BONGO_CAT_OK) stats->succeeded_count++;
    else {
        stats->failed_count++;
        record_failure_name(stats, source);
    }
    if (result == BONGO_CAT_OK && callback)
        callback(userdata, &receipt);
    SDL_Log("[runtime] Model import unit completed: result=%d variants=%llu "
        "installed=%llu elapsed_ms=%.1f path=%s", (int)result,
        (unsigned long long)receipt.count,
        (unsigned long long)receipt.installed_count,
        (SDL_GetTicksNS() - started) / 1000000.0, source);
    return result;
}

static BongoCatResult fail_source(BongoCatImportBatchStats *stats,
    const char *source, BongoCatResult result) {
    stats->failed_count++;
    record_failure_name(stats, source);
    return result;
}

static BongoCatResult install_progressive_directory(
    BongoCatImportSession *session, const char *source,
    BongoCatImportReceiptCallback callback, void *userdata,
    BongoCatImportBatchStats *stats, BongoCatError *error) {
    if (!stats) return BONGO_CAT_ERROR_ARGUMENT;
    *stats = (BongoCatImportBatchStats){0};
    if (!session || !source)
        return fail_source(stats, source, BONGO_CAT_ERROR_ARGUMENT);
    char directory[BONGO_CAT_PATH_CAP];
    BongoCatResult normalized = bongo_cat_import_source_directory(source,
        directory, sizeof(directory), error);
    if (normalized != BONGO_CAT_OK)
        return fail_source(stats, source, normalized);

    SDL_Log("[runtime] Model import progressive discovery started: path=%s",
        directory);
    BongoCatImportDiscovery *exact = calloc(1, sizeof(*exact));
    if (!exact) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate exact model discovery workspace");
        return fail_source(stats, directory, BONGO_CAT_ERROR_MEMORY);
    }
    BongoCatError exact_error = {0};
    int exact_result = bongo_cat_import_probe_exact(directory, exact, NULL,
        BONGO_CAT_IMPORT_PROBE_STRICT, true, &exact_error);
    free(exact);
    if (exact_result > 0) {
        SDL_Log("[runtime] Model import progressive discovery selected exact "
            "package: path=%s", directory);
        return install_one(session, directory, callback, userdata, stats,
            error);
    }
    if (exact_result < 0) {
        if (error) *error = exact_error;
        return fail_source(stats, directory, exact_error.code
            ? exact_error.code : BONGO_CAT_ERROR_FORMAT);
    }

    ProgressiveScan *scan = calloc(1, sizeof(*scan));
    if (!scan) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate progressive model import scan");
        return fail_source(stats, directory, BONGO_CAT_ERROR_MEMORY);
    }
    uint64_t scan_started = SDL_GetTicksNS();
    SDL_Log("[runtime] Model import container scan started: path=%s",
        directory);
    BongoCatResult scanned = bongo_cat_import_scan_diagnostic(directory,
        collect_unit, scan, error);
    SDL_Log("[runtime] Model import container scan completed: result=%d "
        "units=%llu variants=%llu elapsed_ms=%.1f path=%s", (int)scanned,
        (unsigned long long)scan->unit_count,
        (unsigned long long)scan->model_count,
        (SDL_GetTicksNS() - scan_started) / 1000000.0, directory);
    if (scanned != BONGO_CAT_OK || !scan->unit_count) {
        free(scan);
        return scanned != BONGO_CAT_OK ? fail_source(stats, directory, scanned) :
            install_one(session, directory, callback, userdata, stats, error);
    }

    BongoCatResult first_failure = BONGO_CAT_OK;
    BongoCatError first_error = {0};
    for (size_t i = 0; i < scan->unit_count; ++i) {
        if (redundant_patch(scan, &scan->units[i])) continue;
        BongoCatError local = {0};
        BongoCatResult result = install_one(session, scan->units[i].source,
            callback, userdata, stats, &local);
        if (result != BONGO_CAT_OK && first_failure == BONGO_CAT_OK) {
            first_failure = result;
            first_error = local;
        }
    }
    free(scan);
    if (first_failure != BONGO_CAT_OK && error) *error = first_error;
    return first_failure;
}

BongoCatResult bongo_cat_import_session_install_progressive(
    BongoCatImportSession *session, const char *source,
    BongoCatImportReceiptCallback callback, void *userdata,
    BongoCatImportBatchStats *stats, BongoCatError *error) {
    if (!stats) return BONGO_CAT_ERROR_ARGUMENT;
    if (!session || !source || !bongo_cat_import_is_archive(source))
        return install_progressive_directory(session, source, callback,
            userdata, stats, error);
    *stats = (BongoCatImportBatchStats){0};
    char directory[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    BongoCatResult result = bongo_cat_import_archive_extract(source,
        directory, temporary, error);
    if (result != BONGO_CAT_OK) return fail_source(stats, source, result);
    result = install_progressive_directory(session, directory, callback,
        userdata, stats, error);
    bongo_cat_import_archive_cleanup(temporary);
    return result;
}
