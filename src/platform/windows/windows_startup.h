#ifndef BONGO_CAT_WINDOWS_STARTUP_H
#define BONGO_CAT_WINDOWS_STARTUP_H

#ifdef _WIN32
#include "bongo_cat/common.h"
#include <wchar.h>

#define BONGO_CAT_WINDOWS_INSTANCE_PATH_CAP 2048

typedef struct BongoCatWindowsInstanceInfo {
    uint32_t magic;
    wchar_t executable_path[BONGO_CAT_WINDOWS_INSTANCE_PATH_CAP];
    char version[BONGO_CAT_UPDATE_VERSION_CAP];
} BongoCatWindowsInstanceInfo;

const wchar_t *bongo_cat_windows_instance_title(void);
const wchar_t *bongo_cat_windows_update_shutdown_name(void);
const wchar_t *bongo_cat_windows_instance_stopped_name(void);
const wchar_t *bongo_cat_windows_instance_info_name(void);
bool bongo_cat_windows_update_handoff(void);
#endif

#endif
