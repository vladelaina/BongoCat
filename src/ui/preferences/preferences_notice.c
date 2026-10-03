#include "preferences_notice.h"
#include "preferences_state.h"
#include "preferences_model_glyphs.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_paint.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

enum { NOTICE_DURATION_MS = 2600, NOTICE_ENTER_MS = 250 };

static void update_notice_timer(BongoCatPreferences *value,
    BongoCatPreferenceNotice *notice, uint64_t now) {
    if (!notice->message[0]) return;
    bool hovered = SDL_GetMouseFocus() == value->window &&
        nk_input_is_mouse_hovering_rect(&value->ui.context.input, notice->bounds);
    if (notice->hovered || hovered)
        notice->until_ns += now - notice->timer_updated_ns;
    notice->timer_updated_ns = now;
    notice->hovered = hovered;
}

void bongo_cat_preferences_notice_show(BongoCatApp *app,
    const char *message, bool error) {
    if (!app || !app->preferences || !message || !message[0]) return;
    BongoCatPreferences *value = app->preferences;
    uint64_t now = SDL_GetTicksNS();
    BongoCatPreferenceNotice *target = &value->notices[0];
    for (size_t i = 0; i < sizeof(value->notices) / sizeof(value->notices[0]); ++i) {
        BongoCatPreferenceNotice *notice = &value->notices[i];
        update_notice_timer(value, notice, now);
        if (!notice->message[0] || notice->until_ns <= now) {
            target = notice; break;
        }
        if ((target->hovered && !notice->hovered) ||
            (notice->hovered == target->hovered &&
                notice->started_ns < target->started_ns)) target = notice;
    }
    memset(target, 0, sizeof(*target));
    SDL_utf8strlcpy(target->message, message, sizeof(target->message));
    if (!bongo_cat_preferences_model_glyphs_ready(value, target->message)) {
        value->font_reload_pending = true;
        value->font_reload_defer_once = false;
    }
    target->error = error;
    target->started_ns = now;
    target->until_ns = now + NOTICE_DURATION_MS * 1000000ULL;
    target->timer_updated_ns = now;
    value->render_dirty = true;
}

static size_t active_notices(BongoCatPreferences *value, uint64_t now,
    BongoCatPreferenceNotice **items) {
    size_t count = 0;
    for (size_t i = 0; i < sizeof(value->notices) / sizeof(value->notices[0]); ++i) {
        BongoCatPreferenceNotice *notice = &value->notices[i];
        update_notice_timer(value, notice, now);
        if (notice->message[0] && notice->until_ns <= now)
            memset(notice, 0, sizeof(*notice));
        if (notice->message[0]) items[count++] = notice;
    }
    for (size_t i = 1; i < count; ++i) {
        BongoCatPreferenceNotice *item = items[i];
        size_t j = i;
        while (j && items[j - 1]->started_ns > item->started_ns) {
            items[j] = items[j - 1]; j--;
        }
        items[j] = item;
    }
    return count;
}

static int notice_line(const struct nk_user_font *font, const char *text,
    float maximum, float *width) {
    int length = 0, remaining = nk_strlen(text);
    *width = 0;
    while (length < remaining && text[length] != '\n') {
        nk_rune rune;
        int bytes = nk_utf_decode(text + length, &rune, remaining - length);
        if (bytes <= 0) bytes = 1;
        float next_width = font->width(font->userdata, font->height,
            text, length + bytes);
        if (length && next_width > maximum) break;
        length += bytes;
        *width = next_width;
    }
    return length;
}

static float draw_notice(BongoCatPreferences *value,
    struct nk_context *context, BongoCatPreferenceNotice *notice,
    float width, float y, uint64_t now) {
    BongoCatUIPalette p = bongo_cat_ui_palette(
        bongo_cat_ui_dark(context));
    const struct nk_user_font *font = bongo_cat_ui_label_font(context);
    float maximum = NK_MAX(40.0f, width - 100.0f);
    float text_width = 0;
    size_t lines = 0;
    for (const char *cursor = notice->message; *cursor; ++lines) {
        float line_width;
        cursor += notice_line(font, cursor, maximum, &line_width);
        text_width = NK_MAX(text_width, line_width);
        if (*cursor == '\n') cursor++;
    }
    float line_height = font->height + 5.0f;
    float toast_height = NK_MAX(41.0f, lines * line_height + 15.0f);
    float toast_width = NK_MIN(text_width + 36.0f, width - 64.0f);
    float elapsed = (float)(now - notice->started_ns) /
        (NOTICE_ENTER_MS * 1000000.0f);
    float progress = bongo_cat_ui_ease(BONGO_CAT_UI_EASE_SPRING,
        NK_CLAMP(0.0f, elapsed, 1.0f));
    float opacity = NK_CLAMP(0.0f, progress, 1.0f);
    float shown_width = toast_width * (.9f + .1f * progress);
    struct nk_rect bounds = nk_rect((width - shown_width) * .5f,
        y - 12.0f * (1.0f - progress), shown_width, toast_height);
    notice->bounds = bounds;
    struct nk_color tone = notice->error ? p.danger : p.pink;
    if (p.effects) bongo_cat_ui_paint_shadow(context, bounds, 20, 0, 8,
        notice->error ? 25.0f : 22.0f, 0, nk_rgba(tone.r, tone.g, tone.b,
        (nk_byte)((notice->error ? 102 : 89) * opacity)));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 20, nk_rgba(tone.r, tone.g, tone.b,
        (nk_byte)(255 * opacity)));
    float label_y = bounds.y + (bounds.h - lines * line_height + 5.0f) * .5f;
    for (const char *cursor = notice->message; *cursor; label_y += line_height) {
        float line_width;
        int length = notice_line(font, cursor, maximum, &line_width);
        struct nk_rect label = nk_rect(bounds.x + (bounds.w - text_width) * .5f,
            label_y, NK_MIN(text_width + 1, bounds.w - 20), font->height);
        nk_draw_text(canvas, label, cursor, length, font,
            nk_rgba(0, 0, 0, 0), nk_rgba(255, 255, 255, (nk_byte)(255 * opacity)));
        cursor += length;
        if (*cursor == '\n') cursor++;
    }
    value->render_dirty = true;
    return toast_height;
}

void bongo_cat_preferences_notice_draw(BongoCatPreferences *value,
    struct nk_context *context, float width, float height) {
    (void)height;
    if (!value) return;
    uint64_t now = SDL_GetTicksNS();
    BongoCatPreferenceNotice *items[4];
    size_t count = active_notices(value, now, items);
    float y = 20.0f;
    for (size_t i = 0; i < count; ++i)
        y += draw_notice(value, context, items[i], width, y, now) + 10.0f;
}
