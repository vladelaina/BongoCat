#include "ui_backend.h"

#define DOUBLE_CLICK_NS 500000000ull
#define DOUBLE_CLICK_RADIUS 6

void bongo_cat_ui_input_begin(BongoCatUIBackend *ui) {
    if (ui) nk_input_begin(&ui->context);
}

void bongo_cat_ui_input_end(BongoCatUIBackend *ui) {
    if (ui) nk_input_end(&ui->context);
}

void bongo_cat_ui_input_reset(BongoCatUIBackend *ui) {
    if (!ui) return;
    struct nk_mouse *mouse = &ui->context.input.mouse;
    mouse->pos = nk_vec2(-1.0f, -1.0f);
    mouse->prev = mouse->pos;
    mouse->delta = nk_vec2(0.0f, 0.0f);
    mouse->scroll_delta = nk_vec2(0.0f, 0.0f);
    mouse->grab = 0;
    mouse->grabbed = 0;
    mouse->ungrab = 0;
    for (int i = 0; i < NK_BUTTON_MAX; ++i) {
        mouse->buttons[i].down = nk_false;
        mouse->buttons[i].clicked = 0;
        mouse->buttons[i].clicked_pos = mouse->pos;
    }
    for (int i = 0; i < NK_KEY_MAX; ++i) {
        ui->context.input.keyboard.keys[i].down = nk_false;
        ui->context.input.keyboard.keys[i].clicked = 0;
    }
    ui->context.input.keyboard.text_len = 0;
    ui->last_left_click_ns = 0;
    ui->double_click_down = false;
}

static void key_event(BongoCatUIBackend *ui, const SDL_KeyboardEvent *event) {
    bool down = event->down;
    bool control = (event->mod & SDL_KMOD_CTRL) != 0;
    struct nk_context *context = &ui->context;
    switch (event->key) {
    case SDLK_LSHIFT: case SDLK_RSHIFT:
        nk_input_key(context, NK_KEY_SHIFT, (event->mod & SDL_KMOD_SHIFT) != 0); break;
    case SDLK_LALT: case SDLK_RALT:
        nk_input_key(context, NK_KEY_ALT, (event->mod & SDL_KMOD_ALT) != 0); break;
    case SDLK_DELETE: nk_input_key(context, NK_KEY_DEL, down); break;
    case SDLK_RETURN: case SDLK_KP_ENTER: nk_input_key(context, NK_KEY_ENTER, down); break;
    case SDLK_TAB: nk_input_key(context, NK_KEY_TAB, down); break;
    case SDLK_BACKSPACE: nk_input_key(context, NK_KEY_BACKSPACE, down); break;
    case SDLK_HOME:
        nk_input_key(context, NK_KEY_TEXT_START, down);
        nk_input_key(context, NK_KEY_SCROLL_START, down);
        break;
    case SDLK_END:
        nk_input_key(context, NK_KEY_TEXT_END, down);
        nk_input_key(context, NK_KEY_SCROLL_END, down);
        break;
    case SDLK_PAGEUP: nk_input_key(context, NK_KEY_SCROLL_UP, down); break;
    case SDLK_PAGEDOWN: nk_input_key(context, NK_KEY_SCROLL_DOWN, down); break;
    case SDLK_UP: nk_input_key(context, NK_KEY_UP, down); break;
    case SDLK_DOWN: nk_input_key(context, NK_KEY_DOWN, down); break;
    case SDLK_LEFT:
        nk_input_key(context, NK_KEY_TEXT_WORD_LEFT, down && control);
        nk_input_key(context, NK_KEY_LEFT, down && !control);
        break;
    case SDLK_RIGHT:
        nk_input_key(context, NK_KEY_TEXT_WORD_RIGHT, down && control);
        nk_input_key(context, NK_KEY_RIGHT, down && !control);
        break;
    case SDLK_A: nk_input_key(context, NK_KEY_TEXT_SELECT_ALL, down && control); break;
    case SDLK_C: nk_input_key(context, NK_KEY_COPY, down && control); break;
    case SDLK_V: nk_input_key(context, NK_KEY_PASTE, down && control); break;
    case SDLK_X: nk_input_key(context, NK_KEY_CUT, down && control); break;
    case SDLK_Z: nk_input_key(context, NK_KEY_TEXT_UNDO, down && control); break;
    default: break;
    }
}

static void text_event(BongoCatUIBackend *ui, const char *text) {
    int length = text ? (int)SDL_strlen(text) : 0;
    while (length > 0) {
        nk_rune rune;
        int consumed = nk_utf_decode(text, &rune, length);
        if (consumed <= 0) break;
        nk_input_unicode(&ui->context, rune);
        text += consumed;
        length -= consumed;
    }
}

bool bongo_cat_ui_event(BongoCatUIBackend *ui, const SDL_Event *event) {
    if (!ui || !event) return false;
    struct nk_context *context = &ui->context;
    float scale = ui->layout_scale > 0.0f ? ui->layout_scale : 1.0f;
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        key_event(ui, &event->key);
        return true;
    case SDL_EVENT_TEXT_INPUT:
        text_event(ui, event->text.text);
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        nk_input_motion(context, (int)(event->motion.x / scale),
            (int)(event->motion.y / scale));
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: {
        bool down = event->button.down;
        enum nk_buttons button = event->button.button == SDL_BUTTON_LEFT ? NK_BUTTON_LEFT :
            event->button.button == SDL_BUTTON_MIDDLE ? NK_BUTTON_MIDDLE : NK_BUTTON_RIGHT;
        int x = (int)(event->button.x / scale);
        int y = (int)(event->button.y / scale);
        if (button == NK_BUTTON_LEFT && down) {
            uint64_t now = SDL_GetTicksNS();
            int dx = x - ui->last_left_click_x;
            int dy = y - ui->last_left_click_y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            ui->double_click_down = event->button.clicks > 1 ||
                (ui->last_left_click_ns &&
                now - ui->last_left_click_ns <= DOUBLE_CLICK_NS &&
                dx <= DOUBLE_CLICK_RADIUS && dy <= DOUBLE_CLICK_RADIUS);
            if (ui->double_click_down) ui->last_left_click_ns = 0;
            else {
                ui->last_left_click_ns = now;
                ui->last_left_click_x = x;
                ui->last_left_click_y = y;
            }
            if (ui->double_click_down)
                nk_input_button(context, NK_BUTTON_DOUBLE, x, y, nk_true);
        } else if (button == NK_BUTTON_LEFT && ui->double_click_down) {
            nk_input_button(context, NK_BUTTON_DOUBLE, x, y, nk_false);
            ui->double_click_down = false;
        }
        nk_input_button(context, button, x, y, down);
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        nk_input_scroll(context, nk_vec2(event->wheel.x, event->wheel.y));
        return true;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        bongo_cat_ui_input_reset(ui);
        return false;
    default: return false;
    }
}
