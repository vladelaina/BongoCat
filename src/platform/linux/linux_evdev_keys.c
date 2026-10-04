#include "linux_evdev_internal.h"

static const char *const evdev_key_names[KEY_CNT] = {
    [KEY_A] = "KeyA", [KEY_B] = "KeyB", [KEY_C] = "KeyC", [KEY_D] = "KeyD",
    [KEY_E] = "KeyE", [KEY_F] = "KeyF", [KEY_G] = "KeyG", [KEY_H] = "KeyH",
    [KEY_I] = "KeyI", [KEY_J] = "KeyJ", [KEY_K] = "KeyK", [KEY_L] = "KeyL",
    [KEY_M] = "KeyM", [KEY_N] = "KeyN", [KEY_O] = "KeyO", [KEY_P] = "KeyP",
    [KEY_Q] = "KeyQ", [KEY_R] = "KeyR", [KEY_S] = "KeyS", [KEY_T] = "KeyT",
    [KEY_U] = "KeyU", [KEY_V] = "KeyV", [KEY_W] = "KeyW", [KEY_X] = "KeyX",
    [KEY_Y] = "KeyY", [KEY_Z] = "KeyZ",
    [KEY_0] = "Num0", [KEY_1] = "Num1", [KEY_2] = "Num2", [KEY_3] = "Num3",
    [KEY_4] = "Num4", [KEY_5] = "Num5", [KEY_6] = "Num6", [KEY_7] = "Num7",
    [KEY_8] = "Num8", [KEY_9] = "Num9",
    [KEY_F1] = "F1", [KEY_F2] = "F2", [KEY_F3] = "F3", [KEY_F4] = "F4",
    [KEY_F5] = "F5", [KEY_F6] = "F6", [KEY_F7] = "F7", [KEY_F8] = "F8",
    [KEY_F9] = "F9", [KEY_F10] = "F10", [KEY_F11] = "F11", [KEY_F12] = "F12",
    [KEY_F13] = "F13", [KEY_F14] = "F14", [KEY_F15] = "F15", [KEY_F16] = "F16",
    [KEY_F17] = "F17", [KEY_F18] = "F18", [KEY_F19] = "F19", [KEY_F20] = "F20",
    [KEY_F21] = "F21", [KEY_F22] = "F22", [KEY_F23] = "F23", [KEY_F24] = "F24",
    [KEY_ESC] = "Escape", [KEY_TAB] = "Tab", [KEY_PAUSE] = "Pause",
    [KEY_SYSRQ] = "PrintScreen", [KEY_MENU] = "Apps",
    [KEY_CAPSLOCK] = "CapsLock", [KEY_SPACE] = "Space",
    [KEY_BACKSPACE] = "Backspace", [KEY_DELETE] = "Delete",
    [KEY_INSERT] = "Insert", [KEY_HOME] = "Home", [KEY_END] = "End",
    [KEY_PAGEUP] = "PageUp", [KEY_PAGEDOWN] = "PageDown",
    [KEY_UP] = "UpArrow", [KEY_DOWN] = "DownArrow",
    [KEY_LEFT] = "LeftArrow", [KEY_RIGHT] = "RightArrow",
    [KEY_LEFTMETA] = "Meta", [KEY_RIGHTMETA] = "Meta",
    [KEY_LEFTSHIFT] = "ShiftLeft", [KEY_RIGHTSHIFT] = "ShiftRight",
    [KEY_LEFTCTRL] = "ControlLeft", [KEY_RIGHTCTRL] = "ControlRight",
    [KEY_LEFTALT] = "Alt", [KEY_RIGHTALT] = "AltGr",
    [KEY_ENTER] = "Return", [KEY_GRAVE] = "BackQuote",
    [KEY_NUMLOCK] = "NumLock", [KEY_SCROLLLOCK] = "ScrollLock",
    [KEY_KP0] = "Kp0", [KEY_KP1] = "Kp1", [KEY_KP2] = "Kp2",
    [KEY_KP3] = "Kp3", [KEY_KP4] = "Kp4", [KEY_KP5] = "Kp5",
    [KEY_KP6] = "Kp6", [KEY_KP7] = "Kp7", [KEY_KP8] = "Kp8",
    [KEY_KP9] = "Kp9", [KEY_KPDOT] = "KpDecimal",
    [KEY_KPASTERISK] = "KpMultiply", [KEY_KPPLUS] = "KpPlus",
    [KEY_KPMINUS] = "KpMinus", [KEY_KPSLASH] = "KpDivide",
    [KEY_KPENTER] = "Return",
    [KEY_MINUS] = "Minus", [KEY_EQUAL] = "Equal",
    [KEY_LEFTBRACE] = "BracketLeft", [KEY_RIGHTBRACE] = "BracketRight",
    [KEY_BACKSLASH] = "Backslash", [KEY_SEMICOLON] = "Semicolon",
    [KEY_APOSTROPHE] = "Quote", [KEY_COMMA] = "Comma",
    [KEY_DOT] = "Period", [KEY_SLASH] = "Slash"
};

const char *bongo_cat_evdev_key_name(unsigned code) {
    return code < KEY_CNT ? evdev_key_names[code] : NULL;
}

unsigned bongo_cat_evdev_key_index(unsigned code) {
    if (code == KEY_RIGHTMETA) return KEY_LEFTMETA;
    if (code == KEY_KPENTER) return KEY_ENTER;
    return code;
}
