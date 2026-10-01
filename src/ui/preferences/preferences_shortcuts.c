#include "preferences_state.h"
#include "preferences_notice.h"
#include "preferences_widgets.h"
#include "bongo_cat/i18n.h"
#include "bongo_cat/shortcut.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const char *tr(const BongoCatPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(value->app->i18n, key, fallback);
}

bool bongo_cat_preferences_shortcut_active(const BongoCatPreferences *value,
    const char *id) {
    return value && value->shortcut_recording && value->shortcut_id[0] && id &&
        strcmp(value->shortcut_id, id) == 0;
}

static void finish(BongoCatPreferences *value) {
    if (value->shortcut_target && strcmp(value->shortcut_target, value->shortcut_original))
        for (size_t i = 0; i < value->app->settings.behavior_shortcut_count; ++i) {
            BongoCatBehaviorShortcut *binding = &value->app->settings.behavior_shortcuts[i];
            if (binding->shortcut == value->shortcut_target)
                binding->shortcut_disabled = !binding->shortcut[0];
        }
    value->shortcut_recording = false;
    value->shortcut_id[0] = '\0';
    value->shortcut_target = NULL;
    value->shortcut_capacity = 0;
    value->shortcut_key = SDLK_UNKNOWN;
    value->shortcut_suppress_until_ns = SDL_GetTicksNS() + 250000000ULL;
    bongo_cat_shortcut_init(&value->app->shortcut_state);
    memset(&value->app->sound_shortcut_state, 0, sizeof(value->app->sound_shortcut_state));
    memset(value->app->sound_shortcut_active, 0, sizeof(value->app->sound_shortcut_active));
    value->render_dirty = true;
}

void bongo_cat_preferences_shortcut_cancel(BongoCatPreferences *value) {
    if (!value || !value->shortcut_recording) return;
    if (value->shortcut_target && value->shortcut_capacity > 0)
        snprintf(value->shortcut_target, (size_t)value->shortcut_capacity, "%s",
            value->shortcut_original);
    finish(value);
}

void bongo_cat_preferences_shortcut_begin(BongoCatPreferences *value,
    const char *id, char *target, int capacity) {
    if (!value || !id || !target || capacity < 1) return;
    if (bongo_cat_preferences_shortcut_active(value, id)) return;
    bongo_cat_preferences_shortcut_cancel(value);
    snprintf(value->shortcut_id, sizeof(value->shortcut_id), "%s", id);
    value->shortcut_target = target;
    value->shortcut_capacity = capacity;
    snprintf(value->shortcut_original, sizeof(value->shortcut_original), "%s", target);
    value->shortcut_key = SDLK_UNKNOWN;
    value->shortcut_recording = true;
    bongo_cat_shortcut_init(&value->app->shortcut_state);
    memset(&value->app->sound_shortcut_state, 0, sizeof(value->app->sound_shortcut_state));
    memset(value->app->sound_shortcut_active, 0, sizeof(value->app->sound_shortcut_active));
    value->render_dirty = true;
}

static bool modifier_key(SDL_Keycode key) {
    return key == SDLK_LCTRL || key == SDLK_RCTRL ||
        key == SDLK_LSHIFT || key == SDLK_RSHIFT ||
        key == SDLK_LALT || key == SDLK_RALT ||
        key == SDLK_LGUI || key == SDLK_RGUI;
}

static const char *primary_name(SDL_Keycode key, char output[24]) {
    if (key >= SDLK_A && key <= SDLK_Z) {
        output[0] = (char)('A' + key - SDLK_A); output[1] = '\0'; return output;
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        output[0] = (char)('0' + key - SDLK_0); output[1] = '\0'; return output;
    }
    /* SDL places navigation and keypad keys between F12 and F13. */
    if (key >= SDLK_F1 && key <= SDLK_F12) {
        snprintf(output, 24, "F%d", (int)(key - SDLK_F1 + 1)); return output;
    }
    if (key >= SDLK_F13 && key <= SDLK_F24) {
        snprintf(output, 24, "F%d", (int)(key - SDLK_F13 + 13)); return output;
    }
    switch (key) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return "Enter";
    case SDLK_TAB: return "Tab"; case SDLK_SPACE: return "Space";
    case SDLK_PAUSE: return "Pause"; case SDLK_PRINTSCREEN: return "PrintScreen";
    case SDLK_INSERT: return "Insert"; case SDLK_HOME: return "Home";
    case SDLK_END: return "End"; case SDLK_PAGEUP: return "PageUp";
    case SDLK_PAGEDOWN: return "PageDown"; case SDLK_UP: return "ArrowUp";
    case SDLK_DOWN: return "ArrowDown"; case SDLK_LEFT: return "ArrowLeft";
    case SDLK_RIGHT: return "ArrowRight"; case SDLK_CAPSLOCK: return "CapsLock";
    case SDLK_NUMLOCKCLEAR: return "NumLock"; case SDLK_SCROLLLOCK: return "ScrollLock";
    case SDLK_GRAVE: return "BackQuote"; case SDLK_MINUS: return "-";
    case SDLK_EQUALS: return "="; case SDLK_LEFTBRACKET: return "BracketLeft";
    case SDLK_RIGHTBRACKET: return "BracketRight"; case SDLK_BACKSLASH: return "Backslash";
    case SDLK_SEMICOLON: return "Semicolon"; case SDLK_APOSTROPHE: return "Quote";
    case SDLK_COMMA: return "Comma"; case SDLK_PERIOD: return "Period";
    case SDLK_SLASH: return "Slash";
    case SDLK_KP_0: return "Kp0"; case SDLK_KP_1: return "Kp1";
    case SDLK_KP_2: return "Kp2"; case SDLK_KP_3: return "Kp3";
    case SDLK_KP_4: return "Kp4"; case SDLK_KP_5: return "Kp5";
    case SDLK_KP_6: return "Kp6"; case SDLK_KP_7: return "Kp7";
    case SDLK_KP_8: return "Kp8"; case SDLK_KP_9: return "Kp9";
    case SDLK_KP_MULTIPLY: return "KpMultiply"; case SDLK_KP_PLUS: return "KpPlus";
    case SDLK_KP_MINUS: return "KpMinus"; case SDLK_KP_DECIMAL: return "KpDecimal";
    case SDLK_KP_DIVIDE: return "KpDivide"; default: return NULL;
    }
}

static void append(char *output, size_t capacity, const char *token) {
    size_t length = strlen(output);
    if (length >= capacity) return;
    snprintf(output + length, capacity - length, "%s%s", length ? "+" : "", token);
}

static bool capture_key(BongoCatPreferences *value,
    const SDL_KeyboardEvent *event) {
    if (event->repeat) return true;
    if (!event->down) {
        if (value->shortcut_key != SDLK_UNKNOWN && event->key == value->shortcut_key)
            finish(value);
        return true;
    }
    if (event->key == SDLK_ESCAPE) {
        bongo_cat_preferences_shortcut_cancel(value); return true;
    }
    if (event->key == SDLK_BACKSPACE || event->key == SDLK_DELETE) {
        value->shortcut_target[0] = '\0'; value->shortcut_key = event->key;
        value->render_dirty = true; return true;
    }
    if (modifier_key(event->key)) return true;
    char primary[24]; const char *key = primary_name(event->key, primary);
    if (!key) return true;
    char shortcut[BONGO_CAT_SHORTCUT_CAP] = {0};
    if (event->mod & SDL_KMOD_CTRL) append(shortcut, sizeof(shortcut), "Control");
    if (event->mod & SDL_KMOD_SHIFT) append(shortcut, sizeof(shortcut), "Shift");
    if (event->mod & SDL_KMOD_ALT) append(shortcut, sizeof(shortcut), "Alt");
    if (event->mod & SDL_KMOD_GUI) append(shortcut, sizeof(shortcut), "Meta");
    append(shortcut, sizeof(shortcut), key);
    if (bongo_cat_settings_shortcut_conflicts(&value->app->settings, shortcut,
        value->shortcut_target)) {
        snprintf(value->shortcut_target, (size_t)value->shortcut_capacity, "%s",
            value->shortcut_original);
        bongo_cat_preferences_notice_show(value->app, tr(value,
            "components.shortcut.hints.alreadyUsed",
            "This shortcut is already in use."), true);
        finish(value);
        return true;
    }
    snprintf(value->shortcut_target, (size_t)value->shortcut_capacity, "%s", shortcut);
    value->shortcut_key = event->key;
    value->render_dirty = true;
    return true;
}

bool bongo_cat_preferences_shortcut_event(BongoCatPreferences *value,
    const SDL_Event *event) {
    if (!value || !event || !value->shortcut_recording) return false;
    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)
        return capture_key(value, &event->key);
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        bongo_cat_preferences_shortcut_cancel(value); return false;
    }
    return false;
}

bool bongo_cat_preferences_shortcuts_blocked(
    const BongoCatPreferences *value) {
    return value && (value->shortcut_recording ||
        SDL_GetTicksNS() < value->shortcut_suppress_until_ns);
}

static void shortcut_row(BongoCatPreferences *value, struct nk_context *context,
    const char *id, const char *label_key, const char *label_fallback,
    char *target, int capacity, BongoCatPrefIcon icon) {
    bool active = bongo_cat_preferences_shortcut_active(value, id);
    bongo_cat_pref_row_icon(context, icon);
    int action = bongo_cat_pref_edit(context, id,
        tr(value, label_key, label_fallback), "",
        target, active, tr(value, "components.shortcut.hints.clickRecordShortcut",
        "Click to record shortcut"), tr(value,
        "components.shortcut.hints.pressRecordShortcut", "Press shortcut"));
    if (action < 0) {
        target[0] = '\0'; value->render_dirty = true;
    } else if (action > 0) {
        bongo_cat_preferences_shortcut_begin(value, id, target, capacity);
    }
}

void bongo_cat_preferences_page_shortcuts(BongoCatPreferences *value,
    struct nk_context *context) {
    BongoCatShortcutPreferences *keys = &value->app->settings.shortcuts;
    shortcut_row(value, context, "shortcut-cat", "native.shortcut.toggleCat",
        "Show/Hide",
        keys->toggle_pet_visibility, sizeof(keys->toggle_pet_visibility),
        BONGO_CAT_PREF_ICON_SHORTCUT_VISIBILITY);
    shortcut_row(value, context, "shortcut-pref", "native.shortcut.preferences",
        "Preferences",
        keys->visible_preferences, sizeof(keys->visible_preferences),
        BONGO_CAT_PREF_ICON_SHORTCUT_PREFERENCES);
    shortcut_row(value, context, "shortcut-pass", "native.shortcut.passThrough",
        "Pass Through",
        keys->pass_through, sizeof(keys->pass_through),
        BONGO_CAT_PREF_ICON_PASS_THROUGH);
    shortcut_row(value, context, "shortcut-top", "native.shortcut.alwaysOnTop",
        "Always on Top",
        keys->always_on_top, sizeof(keys->always_on_top),
        BONGO_CAT_PREF_ICON_ALWAYS_ON_TOP);
    shortcut_row(value, context, "shortcut-mirror", "native.shortcut.mirror",
        "Mirror",
        keys->mirror, sizeof(keys->mirror),
        BONGO_CAT_PREF_ICON_MIRROR);
}
