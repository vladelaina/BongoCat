#ifndef BONGO_CAT_WINDOWS_DIAGNOSTICS_H
#define BONGO_CAT_WINDOWS_DIAGNOSTICS_H

#ifdef _WIN32
#include <stdbool.h>
#include <windows.h>

bool bongo_cat_windows_diagnostics_probe_capture(
    HWND window, const char *stage);
#endif

#endif
