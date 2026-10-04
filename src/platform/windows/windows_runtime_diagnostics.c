#include "bongo_cat/runtime_diagnostics.h"
#include "bongo_cat/log.h"
#include "bongo_cat/path.h"
#include "windows_utf8.h"
#include <SDL3/SDL.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HANDLE report = INVALID_HANDLE_VALUE, stop_event, watcher;
static DWORD main_thread;
static SRWLOCK phase_lock = SRWLOCK_INIT;
static const char *current_phase;
static PVOID volatile crash_phase;
static ULONGLONG phase_started;
static ULONGLONG generation;
static LONG crash_written;
static LPTOP_LEVEL_EXCEPTION_FILTER previous_filter;

static void write_report(const char *kind, const char *phase, ULONGLONG elapsed,
    DWORD code, const void *address, const char *module, ULONG_PTR offset) {
    /* Independent of SDL logging/heap allocation: the normal logger may be
       locked by the stalled or faulting thread. Each record is one append. */
    SYSTEMTIME time;
    GetLocalTime(&time);
    char line[1536];
    int length = snprintf(line, sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu thread=%lu "
        "event=%s phase=%s elapsed_ms=%llu exception=0x%08lx "
        "address=%p module=%s offset=0x%llx\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds, (unsigned long)GetCurrentProcessId(),
        (unsigned long)GetCurrentThreadId(), kind, phase ? phase : "idle",
        (unsigned long long)elapsed, (unsigned long)code, address,
        module ? module : "unknown", (unsigned long long)offset);
    if (length > 0 && report != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(report, line, (DWORD)SDL_min(length, (int)sizeof(line) - 1),
            &written, NULL);
    }
}

static LONG WINAPI unhandled(EXCEPTION_POINTERS *exception) {
    if (exception && exception->ExceptionRecord &&
        InterlockedCompareExchange(&crash_written, 1, 0) == 0) {
        EXCEPTION_RECORD *record = exception->ExceptionRecord;
        MEMORY_BASIC_INFORMATION region;
        char module[MAX_PATH] = {0};
        ULONG_PTR offset = 0;
        if (VirtualQuery(record->ExceptionAddress, &region, sizeof(region))) {
            GetModuleFileNameA((HMODULE)region.AllocationBase, module, sizeof(module));
            module[sizeof(module) - 1] = 0;
            offset = (ULONG_PTR)record->ExceptionAddress - (ULONG_PTR)region.AllocationBase;
        }
        /* No phase lock here: the exception may have occurred while holding it.
           Phase strings have static lifetime; this is a best-effort breadcrumb. */
        const char *phase = InterlockedCompareExchangePointer(&crash_phase, NULL, NULL);
        write_report("unhandled-exception", phase, 0, record->ExceptionCode,
            record->ExceptionAddress, module, offset);
    }
    return previous_filter ? previous_filter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

const char *bongo_cat_diagnostics_phase(const char *phase) {
    if (GetCurrentThreadId() != main_thread) return NULL;
    AcquireSRWLockExclusive(&phase_lock);
    const char *previous = current_phase;
    current_phase = phase;
    InterlockedExchangePointer(&crash_phase, (PVOID)phase);
    phase_started = GetTickCount64();
    generation++;
    ReleaseSRWLockExclusive(&phase_lock);
    return previous;
}

static DWORD WINAPI watch_main_thread(void *unused) {
    (void)unused;
    unsigned reports = 0;
    bool stalled = false;
    ULONGLONG reported_generation = 0;
    while (WaitForSingleObject(stop_event, 1000) == WAIT_TIMEOUT) {
        /* A fatal exception is already the cause; time spent in Windows error
           reporting afterwards must not be labelled as a second hang. */
        if (InterlockedCompareExchange(&crash_written, 0, 0)) break;
        AcquireSRWLockShared(&phase_lock);
        const char *phase = current_phase;
        ULONGLONG elapsed = GetTickCount64() - phase_started;
        ULONGLONG observed = generation;
        ReleaseSRWLockShared(&phase_lock);
        if (stalled && observed != reported_generation) {
            write_report("main-thread-resumed", phase, 0, 0, NULL, NULL, 0);
            stalled = false;
        }
        if (phase && elapsed >= 5000 && !stalled && reports < 8) {
            write_report("main-thread-stalled", phase, elapsed, 0, NULL, NULL, 0);
            reported_generation = observed;
            stalled = true;
            reports++;
        }
    }
    return 0;
}

void bongo_cat_diagnostics_start(const char *state_root) {
    if (report != INVALID_HANDLE_VALUE) return;
    char path[BONGO_CAT_PATH_CAP], previous[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), state_root, "runtime-diagnostics.log") ||
        !bongo_cat_path_join(previous, sizeof(previous), state_root,
            "runtime-diagnostics.previous.log")) return;
    wchar_t *wide = bongo_cat_windows_wide(path);
    wchar_t *old = bongo_cat_windows_wide(previous);
    if (wide && old) {
        /* Preserve the previous session when the user restarts after a crash. */
        bool rotated = MoveFileExW(wide, old, MOVEFILE_REPLACE_EXISTING) != FALSE;
        DWORD error = rotated ? ERROR_SUCCESS : GetLastError();
        if (rotated || error == ERROR_FILE_NOT_FOUND)
            report = CreateFileW(wide, FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    free(wide); free(old);
    if (report == INVALID_HANDLE_VALUE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Runtime diagnostics file unavailable: %s", path);
        return;
    }
    main_thread = GetCurrentThreadId();
    crash_written = 0;
    bongo_cat_diagnostics_phase("startup");
    previous_filter = SetUnhandledExceptionFilter(unhandled);
    stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (stop_event) watcher = CreateThread(NULL, 0, watch_main_thread, NULL, 0, NULL);
    write_report("diagnostics-started", "startup", 0, 0, NULL, NULL, 0);
    SDL_LogInfo(BONGO_CAT_LOG_LIFECYCLE,
        "[runtime] Diagnostics: file=%s watchdog=%d threshold_ms=5000 max_stalls=8",
        path, watcher != NULL);
}

void bongo_cat_diagnostics_stop(void) {
    if (report == INVALID_HANDLE_VALUE) return;
    bongo_cat_diagnostics_phase(NULL);
    if (stop_event) SetEvent(stop_event);
    if (watcher && WaitForSingleObject(watcher, 2000) != WAIT_OBJECT_0) return;
    SetUnhandledExceptionFilter(previous_filter);
    write_report("diagnostics-stopped", NULL, 0, 0, NULL, NULL, 0);
    if (watcher) CloseHandle(watcher);
    if (stop_event) CloseHandle(stop_event);
    CloseHandle(report);
    watcher = stop_event = NULL;
    report = INVALID_HANDLE_VALUE;
    main_thread = 0;
}
