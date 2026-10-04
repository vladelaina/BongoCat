#include "bongo_cat/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include "windows_utf8.h"
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#endif

#ifdef _WIN32
bool bongo_cat_path_enumerate(const char *path,
    BongoCatPathVisitor visitor, void *userdata) {
    char pattern[BONGO_CAT_PATH_CAP];
    if (!path || !visitor ||
        !bongo_cat_path_join(pattern, sizeof(pattern), path, "*")) return false;
    wchar_t *wide = bongo_cat_windows_wide(pattern);
    WIN32_FIND_DATAW data = {0};
    HANDLE find = wide ? FindFirstFileW(wide, &data) : INVALID_HANDLE_VALUE;
    free(wide);
    if (find == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND && bongo_cat_path_is_dir(path);
    bool ok = true, stopped = false;
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
            continue;
        char name[BONGO_CAT_PATH_CAP];
        if (!bongo_cat_windows_utf8(data.cFileName, name, sizeof(name))) {
            ok = false; break;
        }
        BongoCatPathVisit result = visitor(userdata, path, name);
        if (result == BONGO_CAT_PATH_FAILURE) { ok = false; break; }
        if (result == BONGO_CAT_PATH_SUCCESS) { stopped = true; break; }
    } while (FindNextFileW(find, &data));
    if (!stopped && ok && GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    FindClose(find);
    return ok;
}

bool bongo_cat_path_copy_file(const char *source, const char *target) {
    wchar_t *wide_source = bongo_cat_windows_wide(source);
    wchar_t *wide_target = bongo_cat_windows_wide(target);
    bool ok = wide_source && wide_target && CopyFileW(wide_source, wide_target, FALSE);
    free(wide_source); free(wide_target); return ok;
}

bool bongo_cat_path_rename(const char *source, const char *target) {
    wchar_t *wide_source = bongo_cat_windows_wide(source);
    wchar_t *wide_target = bongo_cat_windows_wide(target);
    bool ok = wide_source && wide_target && MoveFileExW(wide_source, wide_target,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(wide_source); free(wide_target); return ok;
}

bool bongo_cat_path_remove(const char *path) {
    wchar_t *wide = bongo_cat_windows_wide(path);
    DWORD attributes = wide ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
    bool ok = attributes != INVALID_FILE_ATTRIBUTES &&
        ((attributes & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(wide) : DeleteFileW(wide));
    free(wide); return ok;
}
#else
bool bongo_cat_path_enumerate(const char *path,
    BongoCatPathVisitor visitor, void *userdata) {
    if (!path || !visitor) return false;
    DIR *directory = opendir(path);
    if (!directory) return false;
    bool ok = true;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) { if (errno) ok = false; break; }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        BongoCatPathVisit result = visitor(userdata, path, entry->d_name);
        if (result == BONGO_CAT_PATH_FAILURE) { ok = false; break; }
        if (result == BONGO_CAT_PATH_SUCCESS) break;
    }
    if (closedir(directory) != 0) ok = false;
    return ok;
}

bool bongo_cat_path_copy_file(const char *source, const char *target) {
    if (!source || !target || strcmp(source, target) == 0) return false;
    FILE *input = fopen(source, "rb"), *output = input ? fopen(target, "wb") : NULL;
    if (!output) { if (input) fclose(input); return false; }
    unsigned char buffer[8192]; bool ok = true; size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0)
        if (fwrite(buffer, 1, count, output) != count) { ok = false; break; }
    if (ferror(input)) ok = false;
    if (fclose(input) != 0) ok = false;
    if (fclose(output) != 0) ok = false;
    if (!ok) remove(target);
    return ok;
}

bool bongo_cat_path_rename(const char *source, const char *target) {
    return source && target && rename(source, target) == 0;
}

bool bongo_cat_path_remove(const char *path) {
    return path && remove(path) == 0;
}
#endif
