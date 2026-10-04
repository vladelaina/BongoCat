#include "windows_keys.h"

#ifdef _WIN32
#include <stdio.h>

unsigned bongo_cat_windows_key_index(const RAWKEYBOARD *key) {
    if (key->MakeCode && key->MakeCode < 256)
        return key->MakeCode + ((key->Flags & RI_KEY_E1) ? 512u :
            (key->Flags & RI_KEY_E0) ? 256u : 0u);
    return 768u + key->VKey;
}

const char *bongo_cat_windows_key_name(const RAWKEYBOARD *key, char output[16]) {
    if (!key || key->VKey < VK_BACK || key->VKey >= 255 ||
        key->MakeCode == KEYBOARD_OVERRUN_MAKE_CODE) return NULL;
    unsigned code = key->VKey;
    if (code >= 'A' && code <= 'Z') {
        snprintf(output, 16, "Key%c", (char)code);
        return output;
    }
    if (code >= '0' && code <= '9') {
        snprintf(output, 16, "Num%c", (char)code);
        return output;
    }
    if (code >= VK_F1 && code <= VK_F24) {
        snprintf(output, 16, "F%u", code - VK_F1 + 1);
        return output;
    }
    if (!(key->Flags & RI_KEY_E0)) switch (code) {
    case VK_INSERT: return "Kp0"; case VK_END: return "Kp1";
    case VK_DOWN: return "Kp2"; case VK_NEXT: return "Kp3";
    case VK_LEFT: return "Kp4"; case VK_CLEAR: return "Kp5";
    case VK_RIGHT: return "Kp6"; case VK_HOME: return "Kp7";
    case VK_UP: return "Kp8"; case VK_PRIOR: return "Kp9";
    case VK_DELETE: return "KpDecimal"; default: break;
    }
    switch (code) {
    case VK_ESCAPE: return "Escape";
    case VK_PAUSE: return "Pause";
    case VK_SNAPSHOT: return "PrintScreen";
    case VK_APPS: return "Apps";
    case VK_TAB: return "Tab";
    case VK_CAPITAL: return "CapsLock";
    case VK_SPACE: return "Space";
    case VK_BACK: return "Backspace";
    case VK_DELETE: return "Delete";
    case VK_INSERT: return "Insert";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_UP: return "UpArrow";
    case VK_DOWN: return "DownArrow";
    case VK_LEFT: return "LeftArrow";
    case VK_RIGHT: return "RightArrow";
    case VK_LWIN: case VK_RWIN: return "Meta";
    case VK_LSHIFT: return "ShiftLeft";
    case VK_RSHIFT: return "ShiftRight";
    case VK_LCONTROL: return "ControlLeft";
    case VK_RCONTROL: return "ControlRight";
    case VK_LMENU: return "Alt";
    case VK_RMENU: return "AltGr";
    case VK_SHIFT: return key->MakeCode == 0x36 ? "ShiftRight" : "ShiftLeft";
    case VK_CONTROL: return key->Flags & RI_KEY_E0 ? "ControlRight" : "ControlLeft";
    case VK_MENU: return key->Flags & RI_KEY_E0 ? "AltGr" : "Alt";
    case VK_RETURN: return "Return";
    case VK_NUMLOCK: return "NumLock";
    case VK_SCROLL: return "ScrollLock";
    case VK_NUMPAD0: return "Kp0"; case VK_NUMPAD1: return "Kp1";
    case VK_NUMPAD2: return "Kp2"; case VK_NUMPAD3: return "Kp3";
    case VK_NUMPAD4: return "Kp4"; case VK_NUMPAD5: return "Kp5";
    case VK_NUMPAD6: return "Kp6"; case VK_NUMPAD7: return "Kp7";
    case VK_NUMPAD8: return "Kp8"; case VK_NUMPAD9: return "Kp9";
    case VK_MULTIPLY: return "KpMultiply"; case VK_ADD: return "KpPlus";
    case VK_SUBTRACT: return "KpMinus"; case VK_DECIMAL: return "KpDecimal";
    case VK_DIVIDE: return "KpDivide";
    case VK_OEM_3: return "BackQuote";
    case VK_OEM_MINUS: return "Minus";
    case VK_OEM_PLUS: return "Equal";
    case VK_OEM_4: return "BracketLeft";
    case VK_OEM_6: return "BracketRight";
    case VK_OEM_5: return "Backslash";
    case VK_OEM_1: return "Semicolon";
    case VK_OEM_7: return "Quote";
    case VK_OEM_COMMA: return "Comma";
    case VK_OEM_PERIOD: return "Period";
    case VK_OEM_2: return "Slash";
    default:
        snprintf(output, 16, "%u", code);
        return output;
    }
}

bool bongo_cat_windows_right_button_down(void) {
    /* Native window drag/capture state, not global model input. */
    return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}
#endif
