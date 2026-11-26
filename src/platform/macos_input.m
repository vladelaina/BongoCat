#include "macos_internal.h"
#include "macos_keys.h"
#include "bongo_cat/modifier_state.h"

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

typedef struct MacInputState {
    BongoCatPlatform *platform;
    SDL_Thread *thread;
    SDL_Semaphore *ready;
    CFMachPortRef tap;
    CFRunLoopSourceRef source;
    CFRunLoopRef loop;
    bool key_down[BONGO_CAT_INPUT_KEY_STATE_CAP];
    atomic_bool supported;
    atomic_bool stop_requested;
} MacInputState;

static atomic_bool global_supported = ATOMIC_VAR_INIT(false);

static void push(MacInputState *state, BongoCatInputKind kind,
    const char *name, float value) {
    if (!state || !name) return;
    BongoCatInputEvent input = {.kind = kind, .timestamp_ms = SDL_GetTicks(), .value = value};
    snprintf(input.name, sizeof(input.name), "%s", name);
    if (bongo_cat_input_push(state->platform->input, &input)) {
        SDL_Event wake = {0};
        wake.type = state->platform->wake_event_type;
        SDL_PushEvent(&wake);
    }
}

static BongoCatModifierMasks modifier_masks(CGKeyCode code) {
    switch (code) {
    case 56: return (BongoCatModifierMasks){kCGEventFlagMaskShift,
        NX_DEVICELSHIFTKEYMASK, NX_DEVICERSHIFTKEYMASK};
    case 60: return (BongoCatModifierMasks){kCGEventFlagMaskShift,
        NX_DEVICELSHIFTKEYMASK, NX_DEVICERSHIFTKEYMASK};
    case 59: return (BongoCatModifierMasks){kCGEventFlagMaskControl,
        NX_DEVICELCTLKEYMASK, NX_DEVICERCTLKEYMASK};
    case 62: return (BongoCatModifierMasks){kCGEventFlagMaskControl,
        NX_DEVICELCTLKEYMASK, NX_DEVICERCTLKEYMASK};
    case 58: return (BongoCatModifierMasks){kCGEventFlagMaskAlternate,
        NX_DEVICELALTKEYMASK, NX_DEVICERALTKEYMASK};
    case 61: return (BongoCatModifierMasks){kCGEventFlagMaskAlternate,
        NX_DEVICELALTKEYMASK, NX_DEVICERALTKEYMASK};
    case 55: return (BongoCatModifierMasks){kCGEventFlagMaskCommand,
        NX_DEVICELCMDKEYMASK, NX_DEVICERCMDKEYMASK};
    case 54: return (BongoCatModifierMasks){kCGEventFlagMaskCommand,
        NX_DEVICELCMDKEYMASK, NX_DEVICERCMDKEYMASK};
    default: return (BongoCatModifierMasks){0, 0, 0};
    }
}

static bool right_modifier(CGKeyCode code) {
    return code == 60 || code == 62 || code == 61 || code == 54;
}

static bool modifier_is_down(CGKeyCode code, CGEventFlags flags) {
    BongoCatModifierMasks masks = modifier_masks(code);
    if (!masks.combined) return flags & kCGEventFlagMaskAlphaShift;
    return bongo_cat_modifier_is_down(flags, masks, right_modifier(code));
}

/* A shortcut in another app may consume a modifier's key-up. Reconcile the
 * tracked modifier keys with the physical HID state before every key event,
 * and synthesize the missing releases so shortcut state cannot stay latched. */
static void reconcile_modifier_keys(MacInputState *state, CGEventFlags flags) {
    static const CGKeyCode codes[] = {56, 59, 58, 55, 60, 62, 61, 54};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        CGKeyCode code = codes[i];
        if (!state->key_down[code]) continue;
        BongoCatModifierMasks masks = modifier_masks(code);
        if (!bongo_cat_modifier_is_released(flags, masks, right_modifier(code)))
            continue;
        char buffer[16];
        const char *name = bongo_cat_macos_key_name(code, buffer);
        if (name && bongo_cat_input_edge(state->key_down, code, false))
            push(state, BONGO_CAT_INPUT_KEY_UP, name, 0.0f);
    }
}

static CGEventRef event_tap(CGEventTapProxy proxy, CGEventType type,
    CGEventRef event, void *userdata) {
    (void)proxy;
    MacInputState *state = userdata;
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        memset(state->key_down, 0, sizeof(state->key_down));
        if (state->tap) CGEventTapEnable(state->tap, true);
        return event;
    }
    if (type == kCGEventMouseMoved || type == kCGEventLeftMouseDragged ||
        type == kCGEventRightMouseDragged || type == kCGEventOtherMouseDragged) {
        CGPoint point = CGEventGetLocation(event);
        if (bongo_cat_input_mouse(state->platform->input, point.x, point.y)) {
            SDL_Event wake = {0};
            wake.type = state->platform->wake_event_type;
            SDL_PushEvent(&wake);
        }
        return event;
    }
    if (type == kCGEventKeyDown || type == kCGEventKeyUp || type == kCGEventFlagsChanged) {
        CGEventFlags flags = CGEventGetFlags(event);
        reconcile_modifier_keys(state,
            CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState));
        CGKeyCode code = (CGKeyCode)CGEventGetIntegerValueField(event,
            kCGKeyboardEventKeycode);
        char buffer[16]; const char *name = bongo_cat_macos_key_name(code, buffer);
        bool down = type == kCGEventKeyDown;
        if (type == kCGEventFlagsChanged) down = modifier_is_down(code, flags);
        if (name && bongo_cat_input_edge(state->key_down, code, down))
            push(state, down ? BONGO_CAT_INPUT_KEY_DOWN :
                BONGO_CAT_INPUT_KEY_UP, name, down ? 1.0f : 0.0f);
        return event;
    }
    const char *name = type == kCGEventLeftMouseDown || type == kCGEventLeftMouseUp
        ? "Left" : type == kCGEventRightMouseDown || type == kCGEventRightMouseUp
        ? "Right" : "Middle";
    bool down = type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown ||
        type == kCGEventOtherMouseDown;
    push(state, down ? BONGO_CAT_INPUT_MOUSE_DOWN : BONGO_CAT_INPUT_MOUSE_UP,
        name, down ? 1.0f : 0.0f);
    return event;
}

static int SDLCALL input_thread(void *userdata) {
    MacInputState *state = userdata;
    @autoreleasepool {
        CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) |
            CGEventMaskBit(kCGEventKeyUp) | CGEventMaskBit(kCGEventFlagsChanged) |
            CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp) |
            CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventRightMouseUp) |
            CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp) |
            CGEventMaskBit(kCGEventMouseMoved) | CGEventMaskBit(kCGEventLeftMouseDragged) |
            CGEventMaskBit(kCGEventRightMouseDragged) | CGEventMaskBit(kCGEventOtherMouseDragged);
        state->tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
            kCGEventTapOptionListenOnly, mask, event_tap, state);
        if (state->tap) {
            state->source = CFMachPortCreateRunLoopSource(NULL, state->tap, 0);
            state->loop = CFRunLoopGetCurrent(); CFRetain(state->loop);
            CFRunLoopAddSource(state->loop, state->source, kCFRunLoopCommonModes);
            CGEventTapEnable(state->tap, true);
            atomic_store(&state->supported, true);
            atomic_store(&global_supported, true);
        }
        SDL_SignalSemaphore(state->ready);
        if (!atomic_load(&state->supported)) SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "macOS input monitoring permission is required for global input");
        while (atomic_load(&state->supported) &&
            !atomic_load(&state->stop_requested))
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
        if (state->source) CFRelease(state->source);
        if (state->tap) CFRelease(state->tap);
        if (state->loop) CFRelease(state->loop);
        atomic_store(&global_supported, false);
    }
    return 0;
}

bool bongo_cat_macos_input_start(BongoCatPlatform *platform, BongoCatError *error) {
    if (@available(macOS 10.15, *)) {
        if (!CGPreflightListenEventAccess()) CGRequestListenEventAccess();
    }
    MacInputState *state = calloc(1, sizeof(*state));
    if (!state) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate macOS input state");
        return false;
    }
    state->platform = platform;
    atomic_init(&state->supported, false);
    atomic_init(&state->stop_requested, false);
    state->ready = SDL_CreateSemaphore(0);
    state->thread = state->ready ? SDL_CreateThread(input_thread,
        "bongo-cat-macos-input", state) : NULL;
    if (!state->ready || !state->thread) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
            "Cannot start the macOS input listener");
        if (state->thread) SDL_WaitThread(state->thread, NULL);
        if (state->ready) SDL_DestroySemaphore(state->ready);
        free(state);
        return false;
    }
    platform->native = state;
    return true;
}

void bongo_cat_macos_input_stop(BongoCatPlatform *platform) {
    MacInputState *state = platform ? platform->native : NULL;
    if (!state) return;
    atomic_store(&state->stop_requested, true);
    if (state->thread &&
        !SDL_WaitSemaphoreTimeout(state->ready, 3000)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "macOS input thread did not initialize before shutdown");
        platform->native = NULL;
        return;
    }
    if (state->thread) SDL_WaitThread(state->thread, NULL);
    if (state->ready) SDL_DestroySemaphore(state->ready);
    free(state);
    platform->native = NULL;
}

bool bongo_cat_macos_input_supported(void) { return atomic_load(&global_supported); }
#endif
