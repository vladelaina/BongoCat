#include "windows_direct_input.h"
#include "bongo_cat/file.h"

#ifdef _WIN32
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

typedef struct BongoCatDirectInput {
    LPDIRECTINPUT8A input;
    LPDIRECTINPUTDEVICE8A mouse;
    POINT absolute;
    bool absolute_ready;
    bool rebase_pending;
    ULONGLONG last_read_ms;
    FILE *audit;
    unsigned long long reads;
    unsigned long long reacquires;
    unsigned long long failures;
    long long total_x;
    long long total_y;
    unsigned long long buffered_events;
    unsigned long long buffer_overflows;
    ULONGLONG last_log_ms;
    bool diagnostic_ready;
} BongoCatDirectInput;

static HRESULT read_relative_data(BongoCatDirectInput *state,
    DIDEVICEOBJECTDATA *events, DWORD *event_count, long long *x,
    long long *y, bool *overflow) {
    *x = 0; *y = 0; *overflow = false;
    HRESULT result = state->mouse->lpVtbl->GetDeviceData(state->mouse,
        sizeof(events[0]), events, event_count, 0);
    if (result != DIERR_NOTBUFFERED) {
        if (SUCCEEDED(result)) {
            *overflow = result == DI_BUFFEROVERFLOW;
            for (DWORD index = 0; index < *event_count; ++index) {
                if (events[index].dwOfs == DIMOFS_X)
                    *x += (LONG)events[index].dwData;
                else if (events[index].dwOfs == DIMOFS_Y)
                    *y += (LONG)events[index].dwData;
            }
        }
        return result;
    }
    /* A few virtual mouse drivers accept DIPROP_BUFFERSIZE but still expose
       only the immediate state. Keep those drivers usable. */
    *event_count = 0;
    DIMOUSESTATE mouse = {0};
    result = state->mouse->lpVtbl->GetDeviceState(state->mouse,
        sizeof(mouse), &mouse);
    if (SUCCEEDED(result)) {
        *x = mouse.lX;
        *y = mouse.lY;
    }
    return result;
}

void bongo_cat_windows_direct_input_destroy(BongoCatPlatform *platform) {
    BongoCatDirectInput *state = platform ? platform->relative_pointer : NULL;
    if (!state) return;
    if (state->mouse) {
        state->mouse->lpVtbl->Unacquire(state->mouse);
        state->mouse->lpVtbl->Release(state->mouse);
    }
    if (state->input) state->input->lpVtbl->Release(state->input);
    if (state->reads) SDL_Log("[input] Relative pointer stopped: reads=%llu "
        "reacquires=%llu failures=%llu buffered_events=%llu overflows=%llu "
        "total=%lld,%lld", state->reads, state->reacquires,
        state->failures, state->buffered_events, state->buffer_overflows,
        state->total_x, state->total_y);
    if (state->audit) {
        fprintf(state->audit,
            "summary reads=%llu reacquires=%llu failures=%llu "
            "buffered_events=%llu buffer_overflows=%llu total_x=%lld total_y=%lld\n",
            state->reads, state->reacquires, state->failures,
            state->buffered_events, state->buffer_overflows,
            state->total_x, state->total_y);
        fclose(state->audit);
    }
    free(state);
    platform->relative_pointer = NULL;
}

void bongo_cat_windows_direct_input_reset(BongoCatPlatform *platform) {
    BongoCatDirectInput *state = platform ? platform->relative_pointer : NULL;
    if (!state) return;
    double x = 0.0, y = 0.0;
    bongo_cat_windows_direct_input_read(platform, &x, &y);
    state->rebase_pending = true;
}

bool bongo_cat_windows_direct_input_create(BongoCatPlatform *platform,
    void *window) {
    if (!platform || !window) return false;
    BongoCatDirectInput *state = calloc(1, sizeof(*state));
    if (!state) {
        if (!platform->relative_pointer_retry_ms)
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "[input] Relative pointer initialization failed: stage=allocate");
        return false;
    }
    HRESULT result = DirectInput8Create(GetModuleHandleW(NULL),
        DIRECTINPUT_VERSION, &IID_IDirectInput8A, (void **)&state->input,
        NULL);
    if (FAILED(result)) {
        if (!platform->relative_pointer_retry_ms)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[input] Relative pointer initialization failed: "
                "stage=DirectInput8Create result=0x%08lx",
                (unsigned long)result);
        free(state);
        return false;
    }
    platform->relative_pointer = state;
    const char *stage = "CreateDevice";
    result = state->input->lpVtbl->CreateDevice(state->input,
        &GUID_SysMouse, &state->mouse, NULL);
    if (SUCCEEDED(result)) {
        stage = "SetCooperativeLevel";
        result = state->mouse->lpVtbl->SetCooperativeLevel(state->mouse,
            (HWND)window, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    }
    if (SUCCEEDED(result)) {
        stage = "SetDataFormat";
        result = state->mouse->lpVtbl->SetDataFormat(state->mouse,
            &c_dfDIMouse);
    }
    /* Keep enough history for a few frames while a game is busy.  The
       consumer still drains the queue in one bounded read below. */
    DIPROPDWORD property = {{sizeof(property), sizeof(property.diph),
        0, DIPH_DEVICE}, 128};
    if (SUCCEEDED(result)) {
        stage = "SetProperty";
        result = state->mouse->lpVtbl->SetProperty(state->mouse,
            DIPROP_BUFFERSIZE, &property.diph);
    }
    if (SUCCEEDED(result)) {
        stage = "Acquire";
        result = state->mouse->lpVtbl->Acquire(state->mouse);
    }
    if (SUCCEEDED(result)) {
        state->absolute_ready = GetPhysicalCursorPos(&state->absolute) != FALSE;
        state->rebase_pending = true;
        state->last_read_ms = GetTickCount64();
        SDL_Log("[input] Relative pointer initialized: mode=DirectInput "
            "cooperative=background-nonexclusive physical_cursor=%d",
            state->absolute_ready);
        const char *audit_path = getenv("BONGO_CAT_DIRECT_INPUT_AUDIT_FILE");
        if (audit_path && audit_path[0]) {
            state->audit = bongo_cat_file_open(audit_path, "wb");
            if (state->audit) {
                fprintf(state->audit, "direct_input initialized=1\n");
                fflush(state->audit);
            }
        }
        return true;
    }
    if (!platform->relative_pointer_retry_ms)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "[input] Relative pointer initialization failed: "
            "stage=%s result=0x%08lx", stage, (unsigned long)result);
    bongo_cat_windows_direct_input_destroy(platform);
    return false;
}

bool bongo_cat_windows_direct_input_read(BongoCatPlatform *platform,
    double *x, double *y) {
    BongoCatDirectInput *state = platform ? platform->relative_pointer : NULL;
    if (!state || !state->mouse || !x || !y) return false;
    ULONGLONG now_ms = GetTickCount64();
    bool stale = state->last_read_ms && now_ms - state->last_read_ms > 250;
    state->last_read_ms = now_ms;
    /* Use DirectInput's buffered event API.  GetDeviceState can report a
       zero delta when another application is consuming raw/relative mouse
       input between our frames; buffered events preserve those deltas until
       this process reads them. */
    DIDEVICEOBJECTDATA events[128] = {0};
    DWORD event_count = (DWORD)(sizeof(events) / sizeof(events[0]));
    long long direct_x = 0, direct_y = 0;
    bool buffer_overflow = false;
    POINT absolute = {0};
    bool absolute_ready = GetPhysicalCursorPos(&absolute) != FALSE;
    bool fallback_ready = absolute_ready && state->absolute_ready;
    LONG fallback_x = fallback_ready ? absolute.x - state->absolute.x : 0;
    LONG fallback_y = fallback_ready ? absolute.y - state->absolute.y : 0;
    if (absolute_ready) {
        state->absolute = absolute;
        state->absolute_ready = true;
    } else state->absolute_ready = false;
    HRESULT result = read_relative_data(state, events, &event_count,
        &direct_x, &direct_y, &buffer_overflow);
    bool reacquired = false;
    if (FAILED(result)) {
        result = state->mouse->lpVtbl->Acquire(state->mouse);
        reacquired = SUCCEEDED(result);
        if (reacquired) {
            event_count = (DWORD)(sizeof(events) / sizeof(events[0]));
            result = read_relative_data(state, events, &event_count,
                &direct_x, &direct_y, &buffer_overflow);
        }
    }
    state->reads++;
    if (reacquired) state->reacquires++;
    bool sample_ready = SUCCEEDED(result);
    if (sample_ready) {
        if (buffer_overflow) state->buffer_overflows++;
        state->buffered_events += event_count;
    }
    bool available = sample_ready || fallback_ready;
    /* A delayed frame is not evidence that the input is invalid.  In
       particular, a game can keep moving/recentering the cursor while this
       window is not scheduled for a frame.  Only an explicit reset or a
       complete loss of both sources should rebase the virtual pointer. */
    bool rebase = state->rebase_pending || (!sample_ready && !fallback_ready);
    bool direct_zero = direct_x == 0 && direct_y == 0;
    bool physical_delta = fallback_x != 0 || fallback_y != 0;
    bool use_fallback = fallback_ready && (!sample_ready ||
        (physical_delta && (direct_zero || buffer_overflow)) ||
        (direct_zero && reacquired));
    if (rebase) {
        *x = 0.0;
        *y = 0.0;
    } else if (sample_ready && !use_fallback) {
        *x = direct_x;
        *y = direct_y;
    } else if (fallback_ready) {
        *x = fallback_x;
        *y = fallback_y;
    } else {
        *x = 0.0;
        *y = 0.0;
    }
    state->total_x += (long long)*x;
    state->total_y += (long long)*y;
    state->rebase_pending = !available;
    if (!sample_ready) state->failures++;
    bool log_due = !state->diagnostic_ready ||
        now_ms - state->last_log_ms >= 30000;
    if (log_due) {
        SDL_Log("[input] Relative pointer sample: result=0x%08lx "
            "sample=%d reacquired=%d stale=%d physical=%d fallback=%d "
            "rebase=%d direct=%lld,%lld physical_delta=%ld,%ld "
            "events=%lu overflow=%d output=%.0f,%.0f reads=%llu "
            "failures=%llu reacquires=%llu",
            (unsigned long)result, sample_ready, reacquired, stale,
            fallback_ready, use_fallback, rebase,
            direct_x, direct_y,
            (long)fallback_x, (long)fallback_y, *x, *y,
            (unsigned long)event_count, buffer_overflow,
            state->reads, state->failures, state->reacquires);
        state->last_log_ms = now_ms;
    }
    state->diagnostic_ready = true;
    if (state->audit) {
        fprintf(state->audit,
            "read=%llu result=0x%08lx reacquired=%d stale=%d rebase=%d "
            "fallback=%d events=%lu overflow=%d dx=%.0f dy=%.0f "
            "total_x=%lld total_y=%lld\n",
            state->reads, (unsigned long)result, reacquired, stale, rebase,
            use_fallback, (unsigned long)event_count, buffer_overflow,
            *x, *y,
            state->total_x, state->total_y);
        fflush(state->audit);
    }
    return available && !rebase;
}
#endif
