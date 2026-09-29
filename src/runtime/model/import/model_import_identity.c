#include "model_import.h"
#include "model_import_path.h"
#include "model_import_mver_policy.h"
#include "bongo_cat/path.h"
#include "bongo_cat/sha256.h"
#include "bongo_cat/utf8.h"

#include <stdio.h>
#include <string.h>

#define IMPORT_COLLISION_SUFFIX_RESERVE 5

uint32_t bongo_cat_import_candidate_capabilities(
    const BongoCatImportCandidate *candidate) {
    uint32_t result = BONGO_CAT_MODEL_CAPABILITY_LIVE2D |
        BONGO_CAT_MODEL_CAPABILITY_PREVIEW |
        BONGO_CAT_MODEL_CAPABILITY_RUNTIME_ADAPTER |
        BONGO_CAT_MODEL_CAPABILITY_BEHAVIORS;
    if (candidate->format == BONGO_CAT_IMPORT_TAURI) return result;
    result |= BONGO_CAT_MODEL_CAPABILITY_INPUT_IMAGES |
        BONGO_CAT_MODEL_CAPABILITY_KEYBOARD_INPUT |
        BONGO_CAT_MODEL_CAPABILITY_AUDIO |
        BONGO_CAT_MODEL_CAPABILITY_EFFECTS |
        BONGO_CAT_MODEL_CAPABILITY_MVER_PROJECTION;
    if (candidate->mode == BONGO_CAT_MODE_STANDARD)
        result |= BONGO_CAT_MODEL_CAPABILITY_POINTER_OVERLAY;
    if (candidate->mode == BONGO_CAT_MODE_GAMEPAD && candidate->gamepad_buttons)
        result |= BONGO_CAT_MODEL_CAPABILITY_GAMEPAD_INPUT;
    if (candidate->format == BONGO_CAT_IMPORT_MVER_PATCH)
        result |= BONGO_CAT_MODEL_CAPABILITY_IMAGE_PATCH;
    return result;
}

static const char *source_root(const BongoCatImportCandidate *candidate) {
    return candidate->format == BONGO_CAT_IMPORT_MVER_PATCH &&
        candidate->patch_root[0] ? candidate->patch_root : candidate->package_root;
}

static const char *display_source(const BongoCatImportCandidate *candidate,
    char parent[BONGO_CAT_PATH_CAP]) {
    const char *source = source_root(candidate);
    if (candidate->format == BONGO_CAT_IMPORT_MVER_PATCH &&
        bongo_cat_import_parent_path(source, parent, BONGO_CAT_PATH_CAP) &&
        !strcmp(bongo_cat_path_name(source),
            bongo_cat_path_name(candidate->package_root))) return parent;
    return source;
}

static void truncate_utf8(char *value, size_t maximum) {
    size_t length = value ? strlen(value) : 0;
    if (length > maximum) value[maximum] = '\0';
    length = value ? strlen(value) : 0;
    while (length && !bongo_cat_utf8_valid(value)) value[--length] = '\0';
}

static void trim_id_end(char *value) {
    size_t length = value ? strlen(value) : 0;
    while (length && (value[length - 1] == ' ' || value[length - 1] == '.'))
        value[--length] = '\0';
}

static bool ascii_equal(const char *value, size_t length, const char *expected) {
    if (strlen(expected) != length) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char left = (unsigned char)value[i];
        unsigned char right = (unsigned char)expected[i];
        if (left >= 'a' && left <= 'z') left = (unsigned char)(left - 'a' + 'A');
        if (right >= 'a' && right <= 'z') right = (unsigned char)(right - 'a' + 'A');
        if (left != right) return false;
    }
    return true;
}

static bool reserved_device_name(const char *value) {
    size_t length = value ? strcspn(value, ".") : 0;
    while (length && value[length - 1] == ' ') length--;
    if (ascii_equal(value, length, "CON") || ascii_equal(value, length, "PRN") ||
        ascii_equal(value, length, "AUX") || ascii_equal(value, length, "NUL"))
        return true;
    return length == 4 && value[3] >= '1' && value[3] <= '9' &&
        (ascii_equal(value, 3, "COM") || ascii_equal(value, 3, "LPT"));
}

static void copy_display_text(char *output, size_t capacity, const char *value) {
    if (!output || !capacity) return;
    output[0] = '\0';
    if (!value) return;
    snprintf(output, capacity, "%s", value);
    output[capacity - 1] = '\0';
    truncate_utf8(output, capacity - 1);
    for (unsigned char *cursor = (unsigned char *)output; *cursor; ++cursor)
        if (*cursor < 0x20 || *cursor == 0x7f) *cursor = ' ';
}

static bool forbidden_id_byte(unsigned char value) {
    return value < 0x20 || value == 0x7f || value == '/' || value == '\\' ||
        value == ':' || value == '*' || value == '?' || value == '"' ||
        value == '<' || value == '>' || value == '|' || value == '~';
}

static void normalize_id_base(char *output, size_t capacity,
    const char *value) {
    if (!output || !capacity) return;
    output[0] = '\0';
    if (!value) return;
    size_t used = 0;
    for (const unsigned char *cursor = (const unsigned char *)value;
        *cursor && used + 1 < capacity; ++cursor) {
        unsigned char byte = *cursor;
        if (byte < 0x80 && (forbidden_id_byte(byte) || (!used && byte == '.')))
            byte = '-';
        output[used++] = (char)byte;
    }
    output[used] = '\0';
    truncate_utf8(output, capacity - 1);
    trim_id_end(output);
    if (reserved_device_name(output)) {
        truncate_utf8(output, capacity > 1 ? capacity - 2 : 0);
        trim_id_end(output);
        size_t length = strlen(output);
        if (length + 1 < capacity) {
            memmove(output + 1, output, length + 1);
            output[0] = '_';
        }
    }
}

bool bongo_cat_import_package_id(char *output, size_t capacity,
    const char *name) {
    if (!output || capacity < 2) return false;
    char normalized[BONGO_CAT_ID_CAP] = {0};
    normalize_id_base(normalized, sizeof(normalized), name);
    if (!normalized[0]) snprintf(normalized, sizeof(normalized), "Imported model");
    size_t maximum = capacity > IMPORT_COLLISION_SUFFIX_RESERVE + 1
        ? capacity - IMPORT_COLLISION_SUFFIX_RESERVE - 1 : 0;
    truncate_utf8(normalized, maximum);
    trim_id_end(normalized);
    if (!normalized[0]) snprintf(normalized, sizeof(normalized), "model");
    int written = snprintf(output, capacity, "%s", normalized);
    return written >= 0 && (size_t)written < capacity;
}

bool bongo_cat_import_variant_id(char *output, size_t capacity,
    const char *package_id, size_t variant_index) {
    if (!output || !capacity || !package_id || !package_id[0]) return false;
    if (!variant_index) {
        int written = snprintf(output, capacity, "%s", package_id);
        return written >= 0 && (size_t)written < capacity;
    }
    char suffix[32];
    int suffix_length = snprintf(suffix, sizeof(suffix), "~%zu",
        variant_index + 1);
    if (suffix_length < 0 || (size_t)suffix_length >= sizeof(suffix) ||
        capacity <= (size_t)suffix_length + 1) return false;
    char base[BONGO_CAT_ID_CAP];
    snprintf(base, sizeof(base), "%s", package_id);
    truncate_utf8(base, capacity - (size_t)suffix_length - 1);
    trim_id_end(base);
    int written = snprintf(output, capacity, "%s%s", base, suffix);
    return written >= 0 && (size_t)written < capacity;
}

bool bongo_cat_import_package_base_id(char *output, size_t capacity,
    const char *model_id) {
    if (!output || capacity < 2 || !model_id || !model_id[0]) return false;
    int written = snprintf(output, capacity, "%s", model_id);
    if (written < 0 || (size_t)written >= capacity) return false;
    char *separator = strrchr(output, '~');
    if (!separator || !separator[1]) return true;
    for (char *cursor = separator + 1; *cursor; ++cursor)
        if (*cursor < '0' || *cursor > '9') return true;
    *separator = '\0';
    return output[0] != '\0';
}

bool bongo_cat_import_prepare_package_metadata_cached(
    BongoCatImportDiscovery *discovery,
    BongoCatPackageMetadata *metadata, BongoCatImportDigestCache *cache,
    BongoCatError *error) {
    char family_material[BONGO_CAT_IMPORT_CANDIDATE_CAP * 66 + 32];
    size_t used = 0, output = 0;
    for (size_t i = 0; i < discovery->count; ++i) {
        BongoCatImportCandidate candidate = discovery->candidates[i];
        if (bongo_cat_import_patch_has_full_base(discovery, i)) continue;
        BongoCatPackageMetadata *item = &metadata[output];
        bool placeholder = false;
        if (!bongo_cat_import_candidate_inspect_cached(&candidate,
            item->content_digest, &placeholder, cache, error)) return false;
        if (placeholder) continue;
        bool duplicate = false;
        for (size_t j = 0; j < output; ++j)
            if (discovery->candidates[j].mode == candidate.mode &&
                strcmp(metadata[j].content_digest, item->content_digest) == 0) {
                duplicate = true;
                break;
            }
        if (duplicate) continue;
        discovery->candidates[output] = candidate;
        int written = snprintf(family_material + used,
            sizeof(family_material) - used, "%s|", item->content_digest);
        if (written < 0 || (size_t)written >= sizeof(family_material) - used)
            return false;
        used += (size_t)written;
        char parent[BONGO_CAT_PATH_CAP];
        const char *name = discovery->source_name[0] ? discovery->source_name :
            bongo_cat_path_name(display_source(&candidate, parent));
        copy_display_text(item->source_name, sizeof(item->source_name),
            name && name[0] ? name : "Imported model");
        if (!item->source_name[0]) copy_display_text(item->source_name,
            sizeof(item->source_name), "Imported model");
        copy_display_text(item->display_name, sizeof(item->display_name),
            item->source_name);
        item->capabilities = bongo_cat_import_candidate_capabilities(&candidate);
        /* Tauri candidates are normalized to Mver before they are stored. */
        if (candidate.format == BONGO_CAT_IMPORT_TAURI)
            item->capabilities |= BONGO_CAT_MODEL_CAPABILITY_INPUT_IMAGES |
                BONGO_CAT_MODEL_CAPABILITY_KEYBOARD_INPUT |
                BONGO_CAT_MODEL_CAPABILITY_AUDIO |
                BONGO_CAT_MODEL_CAPABILITY_EFFECTS |
                BONGO_CAT_MODEL_CAPABILITY_MVER_PROJECTION |
                (candidate.mode == BONGO_CAT_MODE_GAMEPAD
                    ? BONGO_CAT_MODEL_CAPABILITY_GAMEPAD_INPUT : 0);
        output++;
    }
    discovery->count = output;
    if (!output) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT,
            "Selected model package contains only bundled placeholder modes");
        return false;
    }
    for (size_t i = 0; i < output; ++i) {
        char package_id[BONGO_CAT_ID_CAP];
        if (!bongo_cat_import_package_id(package_id, sizeof(package_id),
                metadata[i].source_name) ||
            !bongo_cat_import_variant_id(metadata[i].package_id,
                sizeof(metadata[i].package_id), package_id, i)) return false;
    }
    if (output > 1) {
        char digest[65];
        bongo_cat_sha256_bytes(family_material, used, digest);
        for (size_t i = 0; i < output; ++i)
            snprintf(metadata[i].family_id, sizeof(metadata[i].family_id),
                "family-%s", digest);
    }
    return true;
}

bool bongo_cat_import_prepare_package_metadata(
    BongoCatImportDiscovery *discovery,
    BongoCatPackageMetadata *metadata, BongoCatError *error) {
    return bongo_cat_import_prepare_package_metadata_cached(discovery,
        metadata, NULL, error);
}

void bongo_cat_import_describe_nearby_entry(BongoCatModelEntry *entry,
    const BongoCatImportCandidate *candidate, const char *id,
    const char *identity, const char *source_hash, const char *source,
    const char *adapter) {
    snprintf(entry->id, sizeof(entry->id), "%s", id);
    snprintf(entry->package_id, sizeof(entry->package_id), "%s", id);
    snprintf(entry->content_digest, sizeof(entry->content_digest), "%s", identity);
    snprintf(entry->family_id, sizeof(entry->family_id), "family-nearby-%.16s",
        source_hash);
    snprintf(entry->directory, sizeof(entry->directory), "%s", candidate->directory);
    snprintf(entry->adapter_directory, sizeof(entry->adapter_directory), "%s", adapter);
    snprintf(entry->storage_directory, sizeof(entry->storage_directory), "%s", source);
    snprintf(entry->setting_file, sizeof(entry->setting_file), "%s", candidate->setting);
    entry->mode = candidate->mode;
    entry->source_format = candidate->format == BONGO_CAT_IMPORT_TAURI
        ? BONGO_CAT_MODEL_SOURCE_TAURI :
        candidate->format == BONGO_CAT_IMPORT_MVER_PATCH
        ? BONGO_CAT_MODEL_SOURCE_MVER_PATCH : BONGO_CAT_MODEL_SOURCE_MVER;
    entry->capabilities = bongo_cat_import_candidate_capabilities(candidate);
    entry->adapter_schema = BONGO_CAT_MODEL_ADAPTER_SCHEMA;
    entry->adapter_generator = BONGO_CAT_MODEL_ADAPTER_GENERATOR;
    entry->managed = true;
}
