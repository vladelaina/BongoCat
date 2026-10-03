#include "model_import_path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

static bool separator(char value) {
    return value == '/' || value == '\\';
}

static bool character_equal(char left, char right) {
    if (separator(left) && separator(right)) return true;
#ifdef _WIN32
    if (left >= 'A' && left <= 'Z') left = (char)(left - 'A' + 'a');
    if (right >= 'A' && right <= 'Z') right = (char)(right - 'A' + 'a');
#endif
    return left == right;
}

bool bongo_cat_import_parent_path(const char *path, char *parent,
    size_t capacity) {
    size_t length = path ? strlen(path) : 0;
    while (length && separator(path[length - 1])) length--;
    while (length && !separator(path[length - 1])) length--;
    while (length > 1 && separator(path[length - 1])) length--;
    if (!length || length >= capacity) return false;
    memcpy(parent, path, length);
    parent[length] = '\0';
    return true;
}

bool bongo_cat_import_relative_path(const char *root, const char *path,
    char *relative, size_t capacity) {
    size_t root_length = root ? strlen(root) : 0;
    while (root_length > 1 && separator(root[root_length - 1])) root_length--;
    size_t path_length = path ? strlen(path) : 0;
    if (!root_length || path_length < root_length) return false;
    for (size_t i = 0; i < root_length; ++i)
        if (!character_equal(root[i], path[i])) return false;
    if (path_length > root_length && !separator(path[root_length]))
        return false;
    const char *value = path + root_length;
    while (separator(*value)) value++;
    int written = snprintf(relative, capacity, "%s", value);
    return written >= 0 && (size_t)written < capacity;
}

bool bongo_cat_import_has_suffix(const char *value, const char *suffix) {
    size_t value_length = value ? strlen(value) : 0;
    size_t suffix_length = suffix ? strlen(suffix) : 0;
    return value_length >= suffix_length && suffix_length > 0 &&
        memcmp(value + value_length - suffix_length, suffix, suffix_length) == 0;
}

bool bongo_cat_import_has_suffix_ci(const char *value, const char *suffix) {
    size_t value_length = value ? strlen(value) : 0;
    size_t suffix_length = suffix ? strlen(suffix) : 0;
    return value_length >= suffix_length && suffix_length > 0 &&
        SDL_strcasecmp(value + value_length - suffix_length, suffix) == 0;
}
