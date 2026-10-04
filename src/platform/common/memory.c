#include "bongo_cat/memory.h"

#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

bool bongo_cat_platform_memory_usage(BongoCatMemoryUsage *usage) {
    if (!usage) return false;
    *usage = (BongoCatMemoryUsage){0};
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters = {0};
    counters.cb = (DWORD)sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
        (PROCESS_MEMORY_COUNTERS *)&counters, (DWORD)sizeof(counters)))
        return false;
    usage->working_set_bytes = (uint64_t)counters.WorkingSetSize;
    usage->private_bytes = (uint64_t)counters.PrivateUsage;
    usage->process_peak_working_set_bytes = (uint64_t)counters.PeakWorkingSetSize;
    return true;
#else
    return false;
#endif
}

void bongo_cat_platform_process_resources(BongoCatProcessResources *usage) {
    if (!usage) return;
#ifdef _WIN32
    DWORD previous_error = GetLastError();
#endif
    *usage = (BongoCatProcessResources){0};
    usage->memory_available = bongo_cat_platform_memory_usage(&usage->memory);
#ifdef _WIN32
    HANDLE process = GetCurrentProcess();
    FILETIME created, exited, kernel, user;
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        uint64_t kernel_ticks = ((uint64_t)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
        uint64_t user_ticks = ((uint64_t)user.dwHighDateTime << 32) | user.dwLowDateTime;
        usage->cpu_time_ns = (kernel_ticks + user_ticks) * 100;
        usage->cpu_available = true;
    }
    DWORD handles = 0;
    usage->handles_available = GetProcessHandleCount(process, &handles) != 0;
    usage->handles = handles;
    SetLastError(ERROR_SUCCESS);
    usage->gdi_objects = GetGuiResources(process, GR_GDIOBJECTS);
    bool gdi_ok = usage->gdi_objects || GetLastError() == ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    usage->user_objects = GetGuiResources(process, GR_USEROBJECTS);
    usage->gui_available = gdi_ok &&
        (usage->user_objects || GetLastError() == ERROR_SUCCESS);
    SetLastError(previous_error);
#endif
}

void bongo_cat_platform_trim_memory(void) {
#ifdef _WIN32
    /* Release unused heap pages without evicting the active model/driver
       working set. Forced eviction changes the memory meter temporarily but
       leaves private/GPU allocations intact and makes following frames fault
       those same pages back in. Let Windows manage residency under pressure. */
    _heapmin();
#elif defined(__APPLE__)
    malloc_zone_pressure_relief(NULL, 0);
#elif defined(__GLIBC__)
    malloc_trim(0);
#endif
}
