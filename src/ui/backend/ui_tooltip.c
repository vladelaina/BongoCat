#include "ui_tooltip.h"
#include "ui_backend.h"
#include "ui_catime.h"

#include <string.h>

static int tooltip_wrap_length(const struct nk_user_font *font,
    const char *text, int length, float width) {
    int consumed = 0;
    int last_space = 0;
    while (consumed < length) {
        nk_rune rune = 0;
        int glyph_length = nk_utf_decode(text + consumed, &rune,
            length - consumed);
        if (glyph_length <= 0) glyph_length = 1;
        int next = consumed + glyph_length;
        float measured = font->width(font->userdata, font->height,
            text, next);
        if (consumed > 0 && measured > width)
            return last_space > 0 ? last_space : consumed;
        consumed = next;
        if (rune == ' ') last_space = consumed;
    }
    return consumed > 0 ? consumed : length;
}

static void tooltip_long(struct nk_context *context, const char *text,
    struct nk_rect anchor, bool hovered, struct nk_color color) {
    if (!context || !text || !text[0]) return;
    static struct nk_context *owner;
    static const char *active_text;
    static struct nk_rect bounds;
    static float scroll;
    if (!hovered && !(owner == context && active_text == text &&
            nk_input_is_mouse_hovering_rect(&context->input, bounds))) {
        if (owner == context && active_text == text) active_text = NULL;
        return;
    }
    struct nk_window *parent = context->current;
    struct nk_window *root = parent;
    while (root->parent) root = root->parent;
    const struct nk_user_font *font = bongo_cat_ui_caption_font(context);
    float width = NK_MIN(760.0f, root->bounds.w - 24.0f);
    float content_width = NK_MAX(1.0f, width - 48.0f);
    float line_height = font->height + 4.0f;
    int lines = 0;
    const char *segment = text;
    do {
        const char *end = strchr(segment, '\n');
        int length = end ? (int)(end - segment) : nk_strlen(segment);
        if (!length) ++lines;
        while (length > 0) {
            int fitting = tooltip_wrap_length(font, segment, length, content_width);
            segment += fitting;
            length -= fitting;
            ++lines;
        }
        segment = end ? end + 1 : NULL;
    } while (segment);
    float height = NK_MIN(lines * line_height + 32.0f, root->bounds.h - 24.0f);
    if (owner != context || active_text != text) {
        owner = context;
        active_text = text;
        scroll = 0;
    }
    bounds = nk_rect(root->bounds.x + (root->bounds.w - width) * .5f,
        NK_CLAMP(root->bounds.y + 12.0f, anchor.y + anchor.h,
            root->bounds.y + root->bounds.h - height - 12.0f), width, height);
    float wheel = context->input.mouse.scroll_delta.y;
    scroll = NK_CLAMP(0.0f, scroll - wheel * line_height * 3.0f,
        NK_MAX(0.0f, lines * line_height + 32.0f - height));
    context->input.mouse.scroll_delta.y = 0;
    struct nk_rect local = bounds;
    local.x -= parent->layout->clip.x;
    local.y -= parent->layout->clip.y;
    if (parent->popup.win && ((int)parent->popup.type & (int)NK_PANEL_SET_NONBLOCK)) return;
    nk_style_push_font(context, font);
    struct nk_vec2 spacing = context->style.window.spacing;
    struct nk_vec2 padding = context->style.window.padding;
    context->style.window.spacing.y = 0;
    context->style.window.padding = nk_vec2(12, 12);
    if (nk_popup_begin(context, NK_POPUP_STATIC, "__##LongTooltip##__",
            NK_WINDOW_BORDER, local)) {
        parent->layout->flags &= ~(nk_flags)NK_WINDOW_ROM;
        parent->popup.type = NK_PANEL_TOOLTIP;
        context->current->layout->type = NK_PANEL_TOOLTIP;
        nk_window_set_scroll(context, 0, (nk_uint)scroll);
        segment = text;
        do {
            const char *end = strchr(segment, '\n');
            int length = end ? (int)(end - segment) : nk_strlen(segment);
            if (!length) {
                nk_layout_row_dynamic(context, line_height, 1);
                nk_spacing(context, 1);
            }
            while (length > 0) {
                int fitting = tooltip_wrap_length(font, segment, length, content_width);
                nk_layout_row_dynamic(context, line_height, 1);
                nk_text_colored(context, segment, fitting, NK_TEXT_LEFT, color);
                segment += fitting;
                length -= fitting;
            }
            segment = end ? end + 1 : NULL;
        } while (segment);
        nk_uint x = 0, y = 0;
        nk_window_get_scroll(context, &x, &y);
        scroll = (float)y;
        nk_tooltip_end(context);
    }
    context->style.window.padding = padding;
    context->style.window.spacing = spacing;
    nk_style_pop_font(context);
}

static void tooltip_multiline(struct nk_context *context, const char *text,
    struct nk_color color) {
    const struct nk_user_font *font = context->style.font;
    struct nk_vec2 window_size = nk_window_get_size(context);
    struct nk_vec2 mouse = context->input.mouse.pos;
    struct nk_vec2 offset = context->style.window.tooltip_offset;
    float margin = 8.0f;
    float outer_padding = context->style.window.padding.x * 4.0f;
    float left_space = mouse.x - offset.x - margin;
    float right_space = window_size.x - mouse.x - offset.x - margin;
    bool place_left = left_space > right_space;
    float available = place_left ? left_space : right_space;
    if (available <= outer_padding) {
        available = NK_MAX(1.0f, window_size.x - margin * 2.0f);
        place_left = false;
    }
    const char *segment = text;
    float natural_width = 0.0f;
    while (segment) {
        const char *end = strchr(segment, '\n');
        int length = end ? (int)(end - segment) : nk_strlen(segment);
        natural_width = NK_MAX(natural_width, font->width(font->userdata,
            font->height,
            segment, length));
        segment = end ? end + 1 : NULL;
    }
    float content_width = NK_MIN(natural_width,
        NK_MAX(1.0f, available - outer_padding));
    float width = content_width + outer_padding;
    enum nk_tooltip_pos position;
    bool place_above = mouse.y > window_size.y * .55f;
    if (place_above)
        position = place_left ? NK_BOTTOM_RIGHT : NK_BOTTOM_LEFT;
    else
        position = place_left ? NK_TOP_RIGHT : NK_TOP_LEFT;
    if (!nk_tooltip_begin_offset(context, width, position, offset)) return;
    float height = font->height + context->style.window.padding.y * 2.0f;
    segment = text;
    while (segment) {
        const char *end = strchr(segment, '\n');
        int length = end ? (int)(end - segment) : nk_strlen(segment);
        if (!length) {
            nk_layout_row_dynamic(context, height, 1);
            nk_text_colored(context, "", 0, NK_TEXT_LEFT, color);
        } else {
            while (length > 0) {
                int fitting = tooltip_wrap_length(font, segment, length,
                    content_width);
                nk_layout_row_dynamic(context, height, 1);
                nk_text_colored(context, segment, fitting, NK_TEXT_LEFT,
                    color);
                segment += fitting;
                length -= fitting;
            }
        }
        if (!end) break;
        segment = end ? end + 1 : NULL;
    }
    nk_tooltip_end(context);
}

void bongo_cat_ui_question_tooltip(struct nk_context *context,
    const char *question, const char *reply) {
    if (!context || !question || !question[0] || !reply || !reply[0]) return;
    const struct nk_user_font *font = bongo_cat_ui_caption_font(context);
    float available = NK_MAX(1.0f, nk_window_get_content_region(context).w - 10.0f);
    int length = nk_strlen(question), remaining = length, lines = 0;
    const char *cursor = question;
    while (remaining > 0) {
        int fitting = tooltip_wrap_length(font, cursor, remaining, available);
        cursor += fitting;
        remaining -= fitting;
        ++lines;
    }
    nk_layout_row_dynamic(context, 19.0f * lines, 1);
    struct nk_rect bounds;
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatUIPalette palette = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    float question_width = font->width(font->userdata, font->height,
        question, nk_strlen(question));
    struct nk_rect text = nk_rect(bounds.x + 5.0f,
        bounds.y + (19.0f - font->height) * .5f, NK_MIN(question_width, available),
        19.0f * lines);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, text);
    if (hover) {
        bongo_cat_ui_cursor_hover_rect(context, text,
            BONGO_CAT_UI_CURSOR_POINTER);
    }
    cursor = question;
    remaining = length;
    for (int line = 0; remaining > 0; ++line) {
        int fitting = tooltip_wrap_length(font, cursor, remaining, available);
        struct nk_rect row = nk_rect(text.x, text.y + line * 19.0f,
            text.w, font->height);
        nk_draw_text(nk_window_get_canvas(context), row, cursor,
            fitting, font, nk_rgba(0, 0, 0, 0), palette.accent);
        cursor += fitting;
        remaining -= fitting;
    }
    struct nk_style_item saved_background = context->style.window.fixed_background;
    struct nk_color saved_window_color = context->style.window.background;
    struct nk_color background = bongo_cat_ui_dark(context)
        ? palette.background : nk_rgb(0xfb, 0xfc, 0xfe);
    background.a = 255;
    context->style.window.fixed_background = nk_style_item_color(background);
    context->style.window.background = background;
    if (strlen(reply) > 512)
        tooltip_long(context, reply, text, hover, palette.pink);
    else if (hover) tooltip_multiline(context, reply, palette.pink);
    context->style.window.fixed_background = saved_background;
    context->style.window.background = saved_window_color;
}
