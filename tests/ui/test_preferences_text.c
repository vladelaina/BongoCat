#include "preferences_text_edit.h"
#include "preferences_text_session.h"
#include "ui_backend.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #value); \
    failures++; \
} } while (0)

static float fixed_width(const void *userdata, const char *text, size_t length) {
    (void)userdata;
    (void)text;
    return (float)length;
}

static void text_edit(void) {
    char text[32] = "A\xE4\xB8\xAD" "B";
    size_t cursor = 0;
    bool selected = false;
    bongo_cat_text_edit_begin(text, &cursor, &selected);
    CHECK(cursor == 5 && !selected);
    CHECK(bongo_cat_text_edit_move(text, &cursor, &selected,
        BONGO_CAT_TEXT_EDIT_LEFT));
    CHECK(cursor == 4);
    CHECK(bongo_cat_text_edit_insert(text, sizeof(text), &cursor, &selected,
        "\xE6\x96\xB0"));
    CHECK(strcmp(text, "A\xE4\xB8\xAD\xE6\x96\xB0" "B") == 0 &&
        cursor == 7);
    CHECK(bongo_cat_text_edit_erase(text, &cursor, &selected, false));
    CHECK(strcmp(text, "A\xE4\xB8\xAD" "B") == 0 && cursor == 4);
    CHECK(bongo_cat_text_edit_move(text, &cursor, &selected,
        BONGO_CAT_TEXT_EDIT_HOME));
    CHECK(bongo_cat_text_edit_erase(text, &cursor, &selected, true));
    CHECK(strcmp(text, "\xE4\xB8\xAD" "B") == 0 && cursor == 0);
    CHECK(bongo_cat_text_edit_move(text, &cursor, &selected,
        BONGO_CAT_TEXT_EDIT_RIGHT));
    CHECK(cursor == 3);
    CHECK(bongo_cat_text_edit_nearest(text, 1.4f, fixed_width, NULL) == 0);
    CHECK(bongo_cat_text_edit_nearest(text, 1.6f, fixed_width, NULL) == 3);
    bongo_cat_text_edit_select_all(text, &cursor, &selected);
    CHECK(bongo_cat_text_edit_insert(text, sizeof(text), &cursor, &selected,
        "\xE6\x9B\xBF\xE6\x8D\xA2"));
    CHECK(strcmp(text, "\xE6\x9B\xBF\xE6\x8D\xA2") == 0 &&
        cursor == 6 && !selected);
    bongo_cat_text_edit_select_all(text, &cursor, &selected);
    CHECK(bongo_cat_text_edit_erase(text, &cursor, &selected, false));
    CHECK(text[0] == '\0' && cursor == 0 && !selected);
    snprintf(text, sizeof(text), "  label\t ");
    bongo_cat_text_edit_trim(text);
    CHECK(strcmp(text, "label") == 0);
}

static void text_session(void) {
    BongoCatPreferencesTextSession session = {0};
    bongo_cat_preferences_text_session_begin(&session, "model", "Name",
        nk_rect(10, 20, 100, 30));
    CHECK(bongo_cat_preferences_text_session_active(&session));
    SDL_Event event = {.type = SDL_EVENT_KEY_DOWN};
    event.key.key = SDLK_A;
    event.key.mod = SDL_KMOD_CTRL;
    BongoCatPreferencesTextSessionEvent result =
        bongo_cat_preferences_text_session_event(&session, &event,
            1.0f, NULL, 5.0f);
    CHECK(result.handled && !result.finish && session.select_all);
    event.key.key = SDLK_DELETE;
    event.key.mod = SDL_KMOD_NONE;
    result = bongo_cat_preferences_text_session_event(&session, &event,
        1.0f, NULL, 5.0f);
    CHECK(result.handled && session.text[0] == '\0' && !session.select_all);
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 200;
    event.button.y = 200;
    result = bongo_cat_preferences_text_session_event(&session, &event,
        1.0f, NULL, 5.0f);
    CHECK(!result.handled && result.finish && result.save);
    bongo_cat_preferences_text_session_reset(&session);
    CHECK(!bongo_cat_preferences_text_session_active(&session));
}

static void input_release_order(void) {
    BongoCatUIBackend ui = {0};
    const SDL_Keycode keys[] = {SDLK_A, SDLK_C, SDLK_V, SDLK_X, SDLK_Z,
        SDLK_LEFT, SDLK_RIGHT};
    const enum nk_keys actions[] = {NK_KEY_TEXT_SELECT_ALL, NK_KEY_COPY,
        NK_KEY_PASTE, NK_KEY_CUT, NK_KEY_TEXT_UNDO,
        NK_KEY_TEXT_WORD_LEFT, NK_KEY_TEXT_WORD_RIGHT};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        SDL_Event event = {.type = SDL_EVENT_KEY_DOWN};
        event.key.key = keys[i];
        event.key.down = true;
        event.key.mod = SDL_KMOD_CTRL;
        bongo_cat_ui_event(&ui, &event);
        CHECK(ui.context.input.keyboard.keys[actions[i]].down);
        event.type = SDL_EVENT_KEY_UP;
        event.key.key = SDLK_LCTRL;
        event.key.down = false;
        event.key.mod = SDL_KMOD_NONE;
        bongo_cat_ui_event(&ui, &event);
        event.key.key = keys[i];
        bongo_cat_ui_event(&ui, &event);
        CHECK(!ui.context.input.keyboard.keys[actions[i]].down);
        CHECK(!ui.context.input.keyboard.keys[NK_KEY_LEFT].down);
        CHECK(!ui.context.input.keyboard.keys[NK_KEY_RIGHT].down);
    }
    SDL_Event event = {.type = SDL_EVENT_KEY_UP};
    event.key.key = SDLK_LSHIFT;
    event.key.mod = SDL_KMOD_RSHIFT;
    bongo_cat_ui_event(&ui, &event);
    CHECK(ui.context.input.keyboard.keys[NK_KEY_SHIFT].down);
    event.key.key = SDLK_RSHIFT;
    event.key.mod = SDL_KMOD_NONE;
    bongo_cat_ui_event(&ui, &event);
    CHECK(!ui.context.input.keyboard.keys[NK_KEY_SHIFT].down);
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_LEFT;
    event.key.down = true;
    bongo_cat_ui_event(&ui, &event);
    CHECK(ui.context.input.keyboard.keys[NK_KEY_LEFT].down);
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    bongo_cat_ui_event(&ui, &event);
    CHECK(!ui.context.input.keyboard.keys[NK_KEY_LEFT].down);
}

int test_preferences_text(void) {
    failures = 0;
    text_edit();
    text_session();
    input_release_order();
    return failures;
}
