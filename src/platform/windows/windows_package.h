#ifndef BONGO_CAT_WINDOWS_PACKAGE_H
#define BONGO_CAT_WINDOWS_PACKAGE_H

#ifdef _WIN32
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

bool bongo_cat_windows_is_packaged(void);
void bongo_cat_windows_package_repair_shortcut(void);
bool bongo_cat_windows_package_storage_root(char *output, size_t capacity);
bool bongo_cat_windows_package_storage_root_for(const wchar_t *profile,
    const wchar_t *family, char *output, size_t capacity);
#endif

#endif
