#include "bongo_cat/platform.h"
#include "windows_startup.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <ctype.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static HANDLE instance_mutex;
static HANDLE instance_wake_event;
static HANDLE instance_update_shutdown_event;
static HANDLE instance_stopped_event;
static HANDLE instance_info_mapping;
static BongoCatWindowsInstanceInfo *instance_info;
static wchar_t instance_title[96] = BONGO_CAT_PET_WINDOW_TITLE_W;
static wchar_t instance_mutex_name[128] = L"Local\\BongoCat.SingleInstance";
static wchar_t instance_wake_name[128] = L"Local\\BongoCat.WakeInstance";
static wchar_t instance_update_shutdown_name[128] =
    L"Local\\BongoCat.UpdateShutdown";
static wchar_t instance_stopped_name[128] = L"Local\\BongoCat.InstanceStopped";
static wchar_t instance_info_name[128] = L"Local\\BongoCat.InstanceInfo";
static bool identity_ready;

static bool safe_identity(const char *value) {
    if (!value || !value[0] || strlen(value) > 32) return false;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor; ++cursor)
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_') return false;
    return true;
}

static void initialize_identity(void) {
    if (identity_ready) return;
    identity_ready = true;
    const char *value = SDL_getenv_unsafe("BONGO_CAT_TEST_INSTANCE_ID");
    if (!safe_identity(value)) return;
    swprintf(instance_title, sizeof(instance_title) / sizeof(instance_title[0]),
        L"%ls [%hs]", BONGO_CAT_PET_WINDOW_TITLE_W, value);
    swprintf(instance_mutex_name,
        sizeof(instance_mutex_name) / sizeof(instance_mutex_name[0]),
        L"Local\\BongoCat.SingleInstance.%hs", value);
    swprintf(instance_wake_name,
        sizeof(instance_wake_name) / sizeof(instance_wake_name[0]),
        L"Local\\BongoCat.WakeInstance.%hs", value);
    swprintf(instance_update_shutdown_name,
        sizeof(instance_update_shutdown_name) /
            sizeof(instance_update_shutdown_name[0]),
        L"Local\\BongoCat.UpdateShutdown.%hs", value);
    swprintf(instance_stopped_name,
        sizeof(instance_stopped_name) / sizeof(instance_stopped_name[0]),
        L"Local\\BongoCat.InstanceStopped.%hs", value);
    swprintf(instance_info_name,
        sizeof(instance_info_name) / sizeof(instance_info_name[0]),
        L"Local\\BongoCat.InstanceInfo.%hs", value);
}

const wchar_t *bongo_cat_windows_instance_title(void) {
    initialize_identity(); return instance_title;
}

const wchar_t *bongo_cat_windows_update_shutdown_name(void) {
    initialize_identity(); return instance_update_shutdown_name;
}

const wchar_t *bongo_cat_windows_instance_stopped_name(void) {
    initialize_identity(); return instance_stopped_name;
}

const wchar_t *bongo_cat_windows_instance_info_name(void) {
    initialize_identity(); return instance_info_name;
}

static void create_instance_events(void) {
    if (!instance_wake_event)
        instance_wake_event = CreateEventW(NULL, FALSE, FALSE, instance_wake_name);
    if (!instance_update_shutdown_event)
        instance_update_shutdown_event = CreateEventW(NULL, FALSE, FALSE,
            instance_update_shutdown_name);
    if (!instance_stopped_event)
        instance_stopped_event = CreateEventW(NULL, TRUE, FALSE,
            instance_stopped_name);
    if (!instance_info_mapping) {
        instance_info_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
            PAGE_READWRITE, 0, sizeof(BongoCatWindowsInstanceInfo),
            instance_info_name);
        if (instance_info_mapping)
            instance_info = MapViewOfFile(instance_info_mapping,
                FILE_MAP_WRITE, 0, 0, sizeof(BongoCatWindowsInstanceInfo));
        if (instance_info) {
            memset(instance_info, 0, sizeof(*instance_info));
            instance_info->magic = 0x42434D49u;
            GetModuleFileNameW(NULL, instance_info->executable_path,
                BONGO_CAT_WINDOWS_INSTANCE_PATH_CAP);
            snprintf(instance_info->version, sizeof(instance_info->version),
                "%s", BONGO_CAT_VERSION);
        }
    }
}

static void wake_existing_instance(void) {
    for (int attempt = 0; attempt < 30; ++attempt) {
        HANDLE wake = OpenEventW(EVENT_MODIFY_STATE, FALSE, instance_wake_name);
        if (wake) {
            bool signaled = SetEvent(wake) != FALSE;
            CloseHandle(wake);
            if (signaled) return;
        }
        HWND existing = FindWindowW(NULL, instance_title);
        if (existing) {
            ShowWindowAsync(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(existing);
            return;
        }
        Sleep(100);
    }
}

bool bongo_cat_platform_single_instance_begin(void) {
    initialize_identity();
    if (SDL_getenv_unsafe("BONGO_CAT_ALLOW_TEST_INSTANCES")) return true;
    instance_mutex = CreateMutexW(NULL, FALSE, instance_mutex_name);
    if (!instance_mutex) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        create_instance_events(); return true;
    }
    CloseHandle(instance_mutex); instance_mutex = NULL;
    if (bongo_cat_windows_update_handoff()) {
        for (int attempt = 0; attempt < 30; ++attempt) {
            instance_mutex = CreateMutexW(NULL, FALSE, instance_mutex_name);
            if (instance_mutex && GetLastError() != ERROR_ALREADY_EXISTS) {
                create_instance_events(); return true;
            }
            if (instance_mutex) CloseHandle(instance_mutex);
            instance_mutex = NULL;
            Sleep(100);
        }
    }
    wake_existing_instance();
    instance_mutex = CreateMutexW(NULL, FALSE, instance_mutex_name);
    if (instance_mutex && GetLastError() != ERROR_ALREADY_EXISTS) {
        create_instance_events(); return true;
    }
    if (instance_mutex) CloseHandle(instance_mutex);
    instance_mutex = NULL;
    return false;
}

bool bongo_cat_platform_single_instance_take_wake(void) {
    return instance_wake_event &&
        WaitForSingleObject(instance_wake_event, 0) == WAIT_OBJECT_0;
}

bool bongo_cat_platform_update_shutdown_argument(int argc, char **argv) {
    bool requested = false;
    for (int i = 1; i < argc; ++i)
        if (argv && argv[i] && strcmp(argv[i], "--shutdown-for-update") == 0)
            requested = true;
    if (!requested) return false;
    initialize_identity();
    HANDLE stopped = OpenEventW(SYNCHRONIZE, FALSE, instance_stopped_name);
    HANDLE shutdown = OpenEventW(EVENT_MODIFY_STATE, FALSE,
        instance_update_shutdown_name);
    if (shutdown) {
        SetEvent(shutdown);
        CloseHandle(shutdown);
    }
    HWND existing = FindWindowW(NULL, instance_title);
    if (existing) PostMessageW(existing, WM_CLOSE, 0, 0);
    if (stopped) {
        WaitForSingleObject(stopped, 15000);
        CloseHandle(stopped);
    }
    return true;
}

bool bongo_cat_platform_single_instance_take_update_shutdown(void) {
    return instance_update_shutdown_event &&
        WaitForSingleObject(instance_update_shutdown_event, 0) == WAIT_OBJECT_0;
}

void bongo_cat_platform_single_instance_end(void) {
    if (instance_stopped_event) SetEvent(instance_stopped_event);
    if (instance_update_shutdown_event)
        CloseHandle(instance_update_shutdown_event);
    instance_update_shutdown_event = NULL;
    if (instance_stopped_event) CloseHandle(instance_stopped_event);
    instance_stopped_event = NULL;
    if (instance_info) UnmapViewOfFile(instance_info);
    instance_info = NULL;
    if (instance_info_mapping) CloseHandle(instance_info_mapping);
    instance_info_mapping = NULL;
    if (instance_wake_event) CloseHandle(instance_wake_event);
    instance_wake_event = NULL;
    if (instance_mutex) CloseHandle(instance_mutex);
    instance_mutex = NULL;
}

BongoCatResult bongo_cat_platform_set_autostart(bool enabled,
    BongoCatError *error) {
    PWSTR startup = NULL;
    HRESULT result = SHGetKnownFolderPath(&FOLDERID_Startup, KF_FLAG_DEFAULT,
        NULL, &startup);
    wchar_t shortcut[BONGO_CAT_PATH_CAP];
    int written = SUCCEEDED(result) && startup
        ? swprintf(shortcut, BONGO_CAT_PATH_CAP, L"%ls\\%ls.lnk", startup,
            BONGO_CAT_NAME_W) : -1;
    CoTaskMemFree(startup);
    if (FAILED(result) || written < 0 || written >= BONGO_CAT_PATH_CAP) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot locate the Windows Startup folder");
        return BONGO_CAT_ERROR_PLATFORM;
    }
    if (!enabled) {
        if (DeleteFileW(shortcut) || GetLastError() == ERROR_FILE_NOT_FOUND)
            return BONGO_CAT_OK;
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot remove the Windows Startup shortcut");
        return BONGO_CAT_ERROR_PLATFORM;
    }

    wchar_t executable[BONGO_CAT_PATH_CAP];
    DWORD length = GetModuleFileNameW(NULL, executable, BONGO_CAT_PATH_CAP);
    if (!length || length >= BONGO_CAT_PATH_CAP) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot determine executable path");
        return BONGO_CAT_ERROR_PLATFORM;
    }
    wchar_t working_directory[BONGO_CAT_PATH_CAP];
    memcpy(working_directory, executable, (length + 1) * sizeof(wchar_t));
    wchar_t *separator = wcsrchr(working_directory, L'\\');
    if (separator) *separator = L'\0';

    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) result = initialized;
    else {
        IShellLinkW *link = NULL;
        IPersistFile *persist = NULL;
        result = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
            &IID_IShellLinkW, (void **)&link);
        if (SUCCEEDED(result)) result = link->lpVtbl->SetPath(link, executable);
        if (SUCCEEDED(result)) result = link->lpVtbl->SetArguments(link,
            L"--autostart");
        if (SUCCEEDED(result) && separator)
            result = link->lpVtbl->SetWorkingDirectory(link, working_directory);
        if (SUCCEEDED(result)) result = link->lpVtbl->SetDescription(link,
            L"Start BongoCat when signing in");
        if (SUCCEEDED(result)) result = link->lpVtbl->QueryInterface(link,
            &IID_IPersistFile, (void **)&persist);
        if (SUCCEEDED(result)) result = persist->lpVtbl->Save(persist,
            shortcut, TRUE);
        if (persist) persist->lpVtbl->Release(persist);
        if (link) link->lpVtbl->Release(link);
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (SUCCEEDED(result)) return BONGO_CAT_OK;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
        "Cannot create the Windows Startup shortcut (0x%08lx)",
        (unsigned long)result);
    return BONGO_CAT_ERROR_PLATFORM;
}
#endif
