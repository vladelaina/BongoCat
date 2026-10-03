#include "windows_input_internal.h"

#ifdef _WIN32
#include <stddef.h>

void bongo_cat_windows_input_packet(WindowsInputState *state,
    const RAWINPUT *packet, UINT bytes) {
    if (!state || !packet) return;
    if (bytes < sizeof(RAWINPUTHEADER) || packet->header.dwSize != bytes) {
        state->diagnostic_invalid++;
        return;
    }
    UINT payload;
    switch (packet->header.dwType) {
    case RIM_TYPEKEYBOARD: payload = sizeof(RAWKEYBOARD); break;
    case RIM_TYPEMOUSE: payload = sizeof(RAWMOUSE); break;
    default: return;
    }
    if (bytes < offsetof(RAWINPUT, data) + payload) {
        state->diagnostic_invalid++;
        return;
    }
    if (packet->header.dwType == RIM_TYPEKEYBOARD) {
        state->diagnostic_keys++;
        if (GET_RAWINPUT_CODE_WPARAM(packet->header.wParam) == RIM_INPUTSINK)
            state->diagnostic_background_keys++;
    }
    else state->diagnostic_mouse++;
    WindowsRawDevice *device = bongo_cat_windows_input_device(state,
        packet->header.hDevice);
    if (!device) { state->diagnostic_device_drops++; return; }
    if (packet->header.dwType == RIM_TYPEKEYBOARD) {
        bongo_cat_windows_input_key(state, device, &packet->data.keyboard);
        return;
    }
    const RAWMOUSE *mouse = &packet->data.mouse;
    bongo_cat_windows_input_buttons(state, device, mouse->usButtonFlags);
    RECT bounds = {0};
    if (mouse->usFlags & MOUSE_MOVE_ABSOLUTE) {
        bool virtual_desktop = (mouse->usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
        bounds.left = virtual_desktop ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0;
        bounds.top = virtual_desktop ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0;
        bounds.right = bounds.left + GetSystemMetrics(virtual_desktop ?
            SM_CXVIRTUALSCREEN : SM_CXSCREEN);
        bounds.bottom = bounds.top + GetSystemMetrics(virtual_desktop ?
            SM_CYVIRTUALSCREEN : SM_CYSCREEN);
    }
    bongo_cat_windows_input_motion(state, device, mouse, &bounds);
    state->wake_pending = true;
}
#endif
