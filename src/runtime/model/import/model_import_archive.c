#include "model_import_archive.h"
#include "model_import_path.h"
#include "model_storage.h"
#include "bongo_cat/path.h"
#include "bongo_cat/utf8.h"
#include <SDL3/SDL.h>
#include <miniz.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <strings.h>
#endif

#define ARCHIVE_BYTES_LIMIT (UINT64_C(2) * 1024 * 1024 * 1024)
#define ARCHIVE_ENTRY_LIMIT 20000

bool bongo_cat_import_is_archive(const char *source) {
    return source && bongo_cat_import_has_suffix_ci(source, ".zip") &&
        !bongo_cat_path_is_dir(source);
}

static size_t read_archive(void *userdata, mz_uint64 offset, void *buffer,
    size_t size) {
    SDL_IOStream *stream = userdata;
    if (offset > INT64_MAX || SDL_SeekIO(stream, (Sint64)offset,
        SDL_IO_SEEK_SET) < 0) return 0;
    return SDL_ReadIO(stream, buffer, size);
}

typedef struct ArchiveOutput {
    SDL_IOStream *stream;
    uint64_t remaining;
    bool failed;
} ArchiveOutput;

static size_t write_archive(void *userdata, mz_uint64 offset,
    const void *buffer, size_t size) {
    (void)offset;
    ArchiveOutput *output = userdata;
    if (size > output->remaining) return 0;
    size_t written = SDL_WriteIO(output->stream, buffer, size);
    output->remaining -= written;
    if (written != size) output->failed = true;
    return written;
}

/* Reject aliases and special Windows paths on every platform, so an archive
   has the same meaning wherever it is imported. Never materialize links. */
static bool safe_path(char *path) {
    for (char *p = path; *p; ++p) if (*p == '\\') *p = '/';
    if (!path[0] || path[0] == '/') return false;
    unsigned depth = 0;
    for (char *part = path; *part;) {
        char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length || ++depth > 24 || part[length - 1] == '.' ||
            part[length - 1] == ' ') return false;
        for (size_t i = 0; i < length; ++i)
            if ((unsigned char)part[i] < 32 || strchr(":*?\"<>|", part[i]))
                return false;
        size_t stem = 0;
        while (stem < length && part[stem] != '.') ++stem;
        if ((stem == 3 && (!SDL_strncasecmp(part, "CON", 3) ||
            !SDL_strncasecmp(part, "PRN", 3) ||
            !SDL_strncasecmp(part, "AUX", 3) ||
            !SDL_strncasecmp(part, "NUL", 3))) ||
            (stem == 4 && (!SDL_strncasecmp(part, "COM", 3) ||
            !SDL_strncasecmp(part, "LPT", 3)) &&
            part[3] >= '0' && part[3] <= '9')) return false;
        part = end ? end + 1 : part + length;
    }
    return true;
}

static bool decode_name(const char *raw, mz_uint flags,
    char name[BONGO_CAT_PATH_CAP]) {
    if (bongo_cat_utf8_valid(raw)) {
        memcpy(name, raw, strlen(raw) + 1);
        return true;
    }
    /* Legacy Chinese ZIP tools store CP936 names without the UTF-8 flag.
       Decode before treating backslashes as path separators. */
    if (flags & (1u << 11)) return false;
#ifdef _WIN32
    wchar_t wide[BONGO_CAT_PATH_CAP];
    int count = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, raw, -1,
        wide, BONGO_CAT_PATH_CAP);
    return count > 0 && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide, count, name, BONGO_CAT_PATH_CAP, NULL, NULL) > 0;
#else
    return false;
#endif
}

static BongoCatResult extract_entries(mz_zip_archive *zip, const char *root) {
    mz_uint count = mz_zip_reader_get_num_files(zip);
    if (!count || count > ARCHIVE_ENTRY_LIMIT) return BONGO_CAT_ERROR_FORMAT;
    uint64_t total = 0;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        char raw_name[BONGO_CAT_PATH_CAP], name[BONGO_CAT_PATH_CAP],
            target[BONGO_CAT_PATH_CAP];
        if (!mz_zip_reader_file_stat(zip, i, &stat))
            return BONGO_CAT_ERROR_FORMAT;
        mz_uint length = mz_zip_reader_get_filename(zip, i, NULL, 0);
        if (!length || length > sizeof(raw_name) ||
            mz_zip_reader_get_filename(zip, i, raw_name,
                sizeof(raw_name)) != length ||
            strlen(raw_name) + 1 != length ||
            !decode_name(raw_name, stat.m_bit_flag, name))
            return BONGO_CAT_ERROR_FORMAT;
        unsigned kind = (stat.m_external_attr >> 16) & 0170000;
        if (!safe_path(name) || stat.m_is_encrypted || !stat.m_is_supported ||
            (kind && kind != 0100000 && kind != 0040000) ||
            stat.m_uncomp_size > ARCHIVE_BYTES_LIMIT - total ||
            !bongo_cat_path_join(target, sizeof(target), root, name))
            return BONGO_CAT_ERROR_FORMAT;
        total += stat.m_uncomp_size;
        if (stat.m_is_directory) {
            if (!bongo_cat_path_create_directory(target)) return BONGO_CAT_ERROR_IO;
            continue;
        }
        char parent[BONGO_CAT_PATH_CAP];
        if (bongo_cat_path_is_file(target) || bongo_cat_path_is_dir(target))
            return BONGO_CAT_ERROR_FORMAT;
        if (!bongo_cat_import_parent_path(target, parent, sizeof(parent)) ||
            !bongo_cat_path_create_directory(parent)) return BONGO_CAT_ERROR_IO;
        SDL_IOStream *output = SDL_IOFromFile(target, "wb");
        if (!output) return BONGO_CAT_ERROR_IO;
        ArchiveOutput destination = {output, stat.m_uncomp_size, false};
        bool extracted = mz_zip_reader_extract_to_callback(zip, i,
            write_archive, &destination, 0) != 0;
        bool closed = SDL_CloseIO(output);
        if (!closed || destination.failed) return BONGO_CAT_ERROR_IO;
        if (!extracted || destination.remaining) return BONGO_CAT_ERROR_FORMAT;
    }
    return BONGO_CAT_OK;
}

void bongo_cat_import_archive_cleanup(const char *temporary) {
    if (temporary && temporary[0] &&
        !bongo_cat_model_remove_tree(temporary, NULL))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Cannot remove model archive temporary directory: %s", temporary);
}

BongoCatResult bongo_cat_import_archive_extract(const char *source,
    char directory[BONGO_CAT_PATH_CAP], char temporary[BONGO_CAT_PATH_CAP],
    BongoCatError *error) {
    directory[0] = temporary[0] = '\0';
    SDL_IOStream *input = SDL_IOFromFile(source, "rb");
    if (!input) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot open ZIP: %s", source);
        return BONGO_CAT_ERROR_IO;
    }
    mz_zip_archive zip = {0};
    zip.m_pRead = read_archive;
    zip.m_pIO_opaque = input;
    Sint64 size = SDL_GetIOSize(input);
    BongoCatResult result = BONGO_CAT_ERROR_FORMAT;
    if (size > 0 && mz_zip_reader_init(&zip, (mz_uint64)size, 0)) {
        char *base = SDL_GetPrefPath("BongoCat", "archive-import");
        char token[80], name[BONGO_CAT_PATH_CAP];
        snprintf(token, sizeof(token), "%016llx-%016llx",
            (unsigned long long)SDL_GetPerformanceCounter(),
            (unsigned long long)SDL_GetTicksNS());
        int written = snprintf(name, sizeof(name), "%s", bongo_cat_path_name(source));
        result = BONGO_CAT_ERROR_IO;
        if (base && written >= 4 && (size_t)written < sizeof(name)) {
            name[written - 4] = '\0';
            if (!safe_path(name)) snprintf(name, sizeof(name), "model");
            char candidate[BONGO_CAT_PATH_CAP];
            if (bongo_cat_path_join(candidate, sizeof(candidate), base, token) &&
                !bongo_cat_path_is_dir(candidate) &&
                !bongo_cat_path_is_file(candidate) &&
                bongo_cat_path_create_directory(candidate)) {
                snprintf(temporary, BONGO_CAT_PATH_CAP, "%s", candidate);
                if (bongo_cat_path_join(directory, BONGO_CAT_PATH_CAP, temporary, name) &&
                    bongo_cat_path_create_directory(directory))
                    result = extract_entries(&zip, directory);
            }
        }
        SDL_free(base);
        mz_zip_reader_end(&zip);
    }
    SDL_CloseIO(input);
    if (result != BONGO_CAT_OK) {
        bongo_cat_import_archive_cleanup(temporary);
        directory[0] = temporary[0] = '\0';
        bongo_cat_error_set(error, result,
            "Cannot extract ZIP (damaged, encrypted, unsafe, too large, or inaccessible): %s",
            source);
    }
    return result;
}
