#include "windows_capture.h"
#include "windows_diagnostics.h"

#ifdef _WIN32
#include <SDL3/SDL.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shobjidl.h>

static UINT taskbar_created_message;
static UINT capture_refresh_message;
static const wchar_t capture_property[] = L"BongoCat.CaptureWindow";
static bool removal_warning_emitted;
static bool style_warning_emitted;
static bool environment_logged;
#define BONGO_CAT_CAPTURE_REFRESH_TIMER ((UINT_PTR)0xBC51)

static bool has_property(HWND window, const wchar_t *name) {
    return window && name && GetPropW(window, name) != NULL;
}

bool bongo_cat_windows_capture_is_configured(HWND window) {
    return has_property(window, capture_property);
}

static bool read_extended_style(HWND window, LONG_PTR *style) {
    SetLastError(ERROR_SUCCESS);
    LONG_PTR value = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (!value && GetLastError() != ERROR_SUCCESS) return false;
    *style = value;
    return true;
}

static bool write_extended_style(HWND window, LONG_PTR style, DWORD *error) {
    SetLastError(ERROR_SUCCESS);
    LONG_PTR previous = SetWindowLongPtrW(window, GWL_EXSTYLE, style);
    DWORD result = GetLastError();
    if (error) *error = result;
    return previous || result == ERROR_SUCCESS;
}

static void register_messages(void) {
    if (!taskbar_created_message)
        taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    if (!capture_refresh_message)
        capture_refresh_message = RegisterWindowMessageW(
            L"BongoCat.CaptureWindow.RefreshTaskbar");
}

static HRESULT remove_taskbar_tab(HWND window) {
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return initialized;
    ITaskbarList *taskbar = NULL;
    HRESULT result = CoCreateInstance(&CLSID_TaskbarList, NULL,
        CLSCTX_INPROC_SERVER, &IID_ITaskbarList, (void **)&taskbar);
    if (SUCCEEDED(result)) result = taskbar->lpVtbl->HrInit(taskbar);
    if (SUCCEEDED(result)) result = taskbar->lpVtbl->DeleteTab(taskbar, window);
    if (taskbar) taskbar->lpVtbl->Release(taskbar);
    if (SUCCEEDED(initialized)) CoUninitialize();
    return result;
}

static void log_environment(HWND window) {
    if (environment_logged) return;
    environment_logged = true;
    OSVERSIONINFOW version = {.dwOSVersionInfoSize = sizeof(version)};
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn get_version = module ?
        (RtlGetVersionFn)(void *)GetProcAddress(module, "RtlGetVersion") : NULL;
    bool version_known = get_version && get_version(&version) == 0;
    BOOL composition = FALSE;
    HRESULT composition_result = DwmIsCompositionEnabled(&composition);
    SDL_Log("Windows capture environment: version=%lu.%lu.%lu known=%d "
        "composition=%d composition_result=0x%08lx remote_session=%d",
        (unsigned long)version.dwMajorVersion,
        (unsigned long)version.dwMinorVersion,
        (unsigned long)version.dwBuildNumber, version_known,
        SUCCEEDED(composition_result) && composition,
        (unsigned long)composition_result, GetSystemMetrics(SM_REMOTESESSION) != 0);
    bongo_cat_windows_diagnostics_log(window);
}

void bongo_cat_windows_capture_log(HWND window, const char *stage) {
    log_environment(window);
    if (!window || !IsWindow(window)) return;
    RECT bounds = {0};
    DWORD cloaked = 0, affinity = 0;
    HRESULT cloak_result = DwmGetWindowAttribute(window, DWMWA_CLOAKED,
        &cloaked, sizeof(cloaked));
    bool affinity_known = GetWindowDisplayAffinity(window, &affinity) != FALSE;
    LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    GetWindowRect(window, &bounds);
    SDL_Log("Windows capture state (%s): hwnd=%p visible=%d iconic=%d "
        "cloaked=%lu cloak_known=%d rect=%ld,%ld %ldx%ld style=0x%llx "
        "exstyle=0x%llx owner=%p affinity=0x%lx affinity_known=%d",
        stage ? stage : "unknown", (void *)window,
        IsWindowVisible(window) != FALSE, IsIconic(window) != FALSE,
        (unsigned long)cloaked, SUCCEEDED(cloak_result),
        (long)bounds.left, (long)bounds.top,
        (long)(bounds.right - bounds.left),
        (long)(bounds.bottom - bounds.top),
        (unsigned long long)style, (unsigned long long)extended,
        (void *)GetWindowLongPtrW(window, GWLP_HWNDPARENT),
        (unsigned long)affinity, affinity_known);
}

static void refresh_taskbar(HWND window) {
    if (!window) return;
    HRESULT result = remove_taskbar_tab(window);
    if (FAILED(result) && IsWindowVisible(window) && !removal_warning_emitted) {
        removal_warning_emitted = true;
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
            "Cannot remove the capture window from the taskbar (0x%08lx)",
            (unsigned long)result);
    }
}

static void schedule_refresh(HWND window) {
    register_messages();
    if (!window) return;
    if (capture_refresh_message)
        PostMessageW(window, capture_refresh_message, 0, 0);
    SetTimer(window, BONGO_CAT_CAPTURE_REFRESH_TIMER, 250, NULL);
}

bool bongo_cat_windows_capture_configure(HWND window) {
    if (!window || !IsWindow(window)) return false;
    register_messages();
    SetPropW(window, capture_property, (HANDLE)1);
    LONG_PTR style = 0;
    if (!read_extended_style(window, &style)) {
        if (!style_warning_emitted) {
            style_warning_emitted = true;
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "Cannot read the window style required for OBS discovery");
        }
        bongo_cat_windows_capture_repair_transparency(window);
        return false;
    }
    /* OBS does not require APPWINDOW. Avoid a hide/show style transition for
       ordinary SDL windows because it can recreate their DWM redirection. */
    LONG_PTR next = style & ~WS_EX_TOOLWINDOW;
    if (next != style) {
        bool shown = IsWindowVisible(window) != FALSE;
        if (shown) ShowWindow(window, SW_HIDE);
        DWORD style_error = ERROR_SUCCESS;
        if (write_extended_style(window, next, &style_error)) {
            SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        } else if (!style_warning_emitted) {
            style_warning_emitted = true;
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "Cannot update the window style required for OBS discovery "
                "(Win32 error %lu, old=0x%llx, new=0x%llx)",
                (unsigned long)style_error, (unsigned long long)style,
                (unsigned long long)next);
        }
        if (shown) {
            ShowWindow(window, SW_SHOWNOACTIVATE);
            UpdateWindow(window);
        }
    }
    /* Style/frame changes can recreate the DWM redirection surface. Repair
       after the complete hide/show transaction, and on the idempotent path
       as well so repeated configure calls remain safe. */
    bongo_cat_windows_capture_repair_transparency(window);
    LONG_PTR applied = 0;
    bool ready = read_extended_style(window, &applied) &&
        !(applied & (WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP));
    if (!ready && !style_warning_emitted) {
        style_warning_emitted = true;
        if (applied & WS_EX_NOREDIRECTIONBITMAP)
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "Window capture is incompatible with WS_EX_NOREDIRECTIONBITMAP "
                "(exstyle=0x%llx)", (unsigned long long)applied);
        else
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                "Cannot configure the window for OBS discovery");
    }
    refresh_taskbar(window);
    schedule_refresh(window);
    return ready;
}

bool bongo_cat_windows_capture_handle_message(
    HWND window, UINT message, WPARAM wparam) {
    register_messages();
    if (bongo_cat_windows_capture_handle_transparency_message(
            window, message, wparam)) return true;
    if (capture_refresh_message && message == capture_refresh_message) {
        if (!has_property(window, capture_property)) return false;
        refresh_taskbar(window);
        return true;
    }
    if (message == WM_TIMER &&
        wparam == BONGO_CAT_CAPTURE_REFRESH_TIMER &&
        has_property(window, capture_property)) {
        KillTimer(window, BONGO_CAT_CAPTURE_REFRESH_TIMER);
        refresh_taskbar(window);
        return true;
    }
    if (taskbar_created_message && message == taskbar_created_message) {
        bool capture_window = has_property(window, capture_property);
        if (capture_window) {
            refresh_taskbar(window);
            schedule_refresh(window);
        }
        bongo_cat_windows_capture_repair_transparency(window);
        return capture_window;
    }
    return false;
}
#endif
