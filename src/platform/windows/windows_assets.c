#include "bongo_cat/file.h"
#include "bongo_cat/platform.h"
#include "bongo_cat/path.h"
#include "bongo_cat/sha256.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static uint16_t read16(const unsigned char *data) {
    return (uint16_t)(data[0] | (uint16_t)data[1] << 8);
}

static uint32_t read32(const unsigned char *data) {
    uint32_t value = 0;
    for (int i = 3; i >= 0; --i) value = value << 8 | data[i];
    return value;
}

static uint64_t read64(const unsigned char *data) {
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = value << 8 | data[i];
    return value;
}

static bool safe_name(const char *name) {
    if (!name[0] || name[0] == '/' || name[0] == '\\') return false;
    const char *cursor = name;
    while ((cursor = strstr(cursor, ".."))) {
        bool left = cursor == name || cursor[-1] == '/' || cursor[-1] == '\\';
        bool right = !cursor[2] || cursor[2] == '/' || cursor[2] == '\\';
        if (left && right) return false;
        cursor += 2;
    }
    return strchr(name, ':') == NULL;
}

static bool write_file(const char *path, const unsigned char *data, size_t size,
    BongoCatError *error) {
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot create embedded asset: %s", path);
        return false;
    }
    bool ok = fwrite(data, 1, size, file) == size;
    if (fclose(file) != 0) ok = false;
    if (!ok) bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot write embedded asset: %s", path);
    return ok;
}

static bool verify_contents(const char *name) {
    const char *model = "assets/models/standard/";
    return strcmp(name, "assets/locales/en-US.json") == 0 ||
        strcmp(name, "assets/bongocat.png") == 0 ||
        strcmp(name, "assets/ui-symbols.png") == 0 ||
        strcmp(name, "assets/ui-symbols@4x.png") == 0 ||
        strcmp(name, "assets/catime.png") == 0 ||
        strcmp(name, "assets/vlaina.jpg") == 0 ||
        (strncmp(name, model, strlen(model)) == 0 && !strstr(name, "/resources/"));
}

static bool content_matches(const char *path, const unsigned char *expected, size_t size) {
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    unsigned char buffer[8192]; size_t offset = 0;
    while (offset < size) {
        size_t count = size - offset < sizeof(buffer) ? size - offset : sizeof(buffer);
        if (fread(buffer, 1, count, file) != count ||
            memcmp(buffer, expected + offset, count) != 0) {
            fclose(file); return false;
        }
        offset += count;
    }
    bool ok = fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    return ok;
}

static bool packed_files_present(const unsigned char *data, size_t size,
    const char *target) {
    if (size < 12) return false;
    uint32_t count = read32(data + 8); size_t offset = 12;
    if (count > 4096) return false;
    for (uint32_t index = 0; index < count; ++index) {
        if (offset + 16 > size) return false;
        uint16_t name_size = read16(data + offset);
        uint32_t file_size = read32(data + offset + 4);
        uint64_t file_offset = read64(data + offset + 8);
        offset += 16;
        char name[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
        if (!name_size || name_size >= sizeof(name) || offset + name_size > size ||
            file_offset > size || file_size > size - (size_t)file_offset) return false;
        memcpy(name, data + offset, name_size); name[name_size] = '\0'; offset += name_size;
        uint64_t actual_size;
        if (!safe_name(name) || !bongo_cat_path_join(path, sizeof(path), target, name) ||
            !bongo_cat_path_file_size(path, &actual_size) ||
            actual_size != file_size) return false;
        if (verify_contents(name) && !content_matches(path,
            data + (size_t)file_offset, file_size)) return false;
    }
    return true;
}

static bool current_pack(const char *marker, const char *target,
    const unsigned char *data, size_t size, const char expected[65]) {
    FILE *file = bongo_cat_file_open(marker, "rb");
    if (!file) return false;
    char actual[65] = {0};
    bool read = fread(actual, 1, 64, file) == 64;
    fclose(file);
    return read && strcmp(actual, expected) == 0 &&
        packed_files_present(data, size, target);
}

static bool extract_pack(const unsigned char *data, size_t size, const char *target,
    BongoCatError *error) {
    const unsigned char magic[8] = {'L', '2', 'D', 'P', 'A', 'K', '1', 0};
    if (size < 12 || memcmp(data, magic, sizeof(magic)) != 0) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT, "Invalid embedded asset pack");
        return false;
    }
    uint32_t count = read32(data + 8);
    size_t offset = 12;
    if (count > 4096) return false;
    for (uint32_t index = 0; index < count; ++index) {
        if (offset + 16 > size) return false;
        uint16_t name_size = read16(data + offset);
        uint32_t file_size = read32(data + offset + 4);
        uint64_t file_offset = read64(data + offset + 8);
        offset += 16;
        char name[BONGO_CAT_PATH_CAP], path[BONGO_CAT_PATH_CAP];
        if (!name_size || name_size >= sizeof(name) || offset + name_size > size ||
            file_offset > size || file_size > size - (size_t)file_offset) return false;
        memcpy(name, data + offset, name_size); name[name_size] = '\0';
        offset += name_size;
        if (!safe_name(name) ||
            !bongo_cat_path_join(path, sizeof(path), target, name)) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_FORMAT, "Unsafe embedded asset path");
            return false;
        }
        const char *slash = strrchr(path, '/');
        if (slash) {
            char parent[BONGO_CAT_PATH_CAP];
            size_t length = (size_t)(slash - path);
            memcpy(parent, path, length); parent[length] = '\0';
            if (!bongo_cat_path_create_directory(parent)) return false;
        }
        if (!write_file(path, data + (size_t)file_offset, file_size, error)) return false;
    }
    return true;
}

BongoCatResult bongo_cat_platform_embedded_assets(const char *target, BongoCatError *error) {
    if (!target) return BONGO_CAT_ERROR_ARGUMENT;
    char marker[BONGO_CAT_PATH_CAP];
    bongo_cat_path_join(marker, sizeof(marker), target, "complete");
    HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10));
    if (!resource) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
            "Embedded application assets are unavailable");
        return BONGO_CAT_ERROR_IO;
    }
    HGLOBAL loaded = LoadResource(NULL, resource);
    const unsigned char *data = loaded ? LockResource(loaded) : NULL;
    DWORD size = loaded ? SizeofResource(NULL, resource) : 0;
    char digest[65];
    if (data && size) bongo_cat_sha256_bytes(data, size, digest);
    if (data && size && current_pack(marker, target, data, size, digest))
        return BONGO_CAT_OK;
    SDL_Log("Embedded asset cache is incomplete; rebuilding it");
    if (!data || !size || !bongo_cat_path_create_directory(target) ||
        !extract_pack(data, size, target, error)) {
        if (error && !error->message[0]) bongo_cat_error_set(error,
            BONGO_CAT_ERROR_IO, "Cannot rebuild the embedded asset cache: %s",
            SDL_GetError());
        return BONGO_CAT_ERROR_IO;
    }
    return write_file(marker, (const unsigned char *)digest, 64, error)
        ? BONGO_CAT_OK : BONGO_CAT_ERROR_IO;
}
#endif
