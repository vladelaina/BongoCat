#include "ui_catime.h"
#include "ui_backend.h"
#include "ui_animation.h"
#include "ui_icons.h"
#include "ui_paint.h"

static BongoCatUIIconDraw external_icon_draw;
static void *external_icon_userdata;

void bongo_cat_ui_set_icons(BongoCatUIIconDraw draw_icon,
    void *icon_userdata) {
    external_icon_draw = draw_icon;
    external_icon_userdata = icon_userdata;
}

bool bongo_cat_ui_draw_icon(struct nk_command_buffer *canvas, int icon,
    struct nk_rect bounds, struct nk_color color) {
    if (!external_icon_draw) return false;
    external_icon_draw(external_icon_userdata, canvas, icon, bounds, color);
    return true;
}

static void centered_span(struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *text, int length,
    const struct nk_user_font *font, struct nk_color color) {
    float width = font->width(font->userdata, font->height, text, length);
    float target_width = NK_MIN(bounds.w, width + 1.0f);
    struct nk_rect target = nk_rect(bounds.x + (bounds.w - target_width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f,
        target_width, font->height);
    nk_draw_text(canvas, target, text, length, font,
        nk_rgba(0, 0, 0, 0), color);
}

static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *text, const struct nk_user_font *font, struct nk_color color) {
    centered_span(canvas, bounds, text, nk_strlen(text), font, color);
}

float bongo_cat_ui_sidebar_width(float window_width) {
    return window_width <= 780.0f ? BONGO_CAT_UI_SIDEBAR_NARROW :
        BONGO_CAT_UI_SIDEBAR_WIDTH;
}

bool bongo_cat_ui_native_chrome(void) {
    /* Keep SDL borderless input handling consistent on macOS. */
    return false;
}

void bongo_cat_ui_shell_draw(struct nk_context *context, float width,
    float height, bool dark, bool native_frame) {
    BongoCatUIPalette p = bongo_cat_ui_palette(dark);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    float side = bongo_cat_ui_sidebar_width(width);
    float rounding = native_frame ? 0.0f : 24.0f;
    float sidebar_right = BONGO_CAT_UI_MARGIN + side;
    struct nk_rect surface = nk_rect(0, 0, width, height);
    BongoCatUIBackend *backend = bongo_cat_ui_backend_for_context(context);
    bool fast = backend && backend->live_resize_fast;
    if (native_frame) {
        nk_fill_rect(canvas, surface, 0, p.surface_glass);
        nk_fill_rect(canvas, nk_rect(0, 0, sidebar_right, height), 0,
            p.surface);
    } else {
        nk_fill_rect(canvas, surface, rounding, p.surface_glass);
        nk_push_scissor(canvas, nk_rect(0, 0, sidebar_right, height));
        nk_fill_rect(canvas, surface, rounding, p.surface);
        nk_push_scissor(canvas, surface);
    }
    if (p.effects && !fast && !dark)
        bongo_cat_ui_paint_sidebar_glow(context, surface, sidebar_right,
            rounding,
            nk_rgba(p.accent.r, p.accent.g, p.accent.b, 56));
    nk_stroke_line(canvas, sidebar_right, BONGO_CAT_UI_MARGIN, sidebar_right,
        height - BONGO_CAT_UI_MARGIN, 1,
        p.border_subtle);
}

bool bongo_cat_ui_header(struct nk_context *context, const char *title,
    const struct nk_user_font *font, unsigned int logo_texture,
    bool *title_clicked, bool interactive, bool dark, bool native_chrome) {
    struct nk_rect bounds;
    bool compact = nk_window_get_content_region(context).w < 100;
    float height = compact ? (native_chrome ? 148.0f : 118.0f) :
        (native_chrome ? 176.0f : 148.0f);
    nk_layout_row_dynamic(context, height, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    BongoCatUIPalette p = bongo_cat_ui_palette(dark);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    /* The current logo asset already contains its own rounded brand mark.
       Give it room to read at a glance instead of framing it a second time. */
    float base_size = compact ? 68.0f : 84.0f;
    float top = native_chrome ? 34.0f : (compact ? 6.0f : 8.0f);
    struct nk_rect base_logo = nk_rect(bounds.x + (bounds.w - base_size) * .5f,
        bounds.y + top, base_size, base_size);
    bool logo_hover = interactive && nk_input_is_mouse_hovering_rect(
        &context->input, base_logo);
    bool hover = interactive && nk_input_is_mouse_hovering_rect(&context->input,
        nk_rect(bounds.x, bounds.y, bounds.w, bounds.h));
    float hover_amount = bongo_cat_ui_animate_eased(context,
        "brand-logo-hover", hover ? 1.0f : 0.0f, 250.0f,
        BONGO_CAT_UI_EASE_SPRING);
    float size = base_size * (1.0f + .08f * hover_amount);
    struct nk_rect logo = nk_rect(bounds.x + (bounds.w - size) * .5f,
        bounds.y + top - (size - base_size) * .5f, size, size);
    if (p.effects) bongo_cat_ui_paint_shadow(context, logo,
        compact ? 17.0f : 21.0f, 0, 4, 14, 0,
        nk_rgba(p.pink.r, p.pink.g, p.pink.b, 64));
    if (logo_texture) {
        struct nk_image image = nk_image_id((int)logo_texture);
        nk_draw_image(canvas, logo, &image, nk_rgb(255, 255, 255));
    }
    if (bounds.w >= 100) {
        if (!font) font = bongo_cat_ui_caption_font(context);
        float title_y = native_chrome ? 124.0f : 94.0f;
        struct nk_color title_color = logo_hover ? nk_rgb(84, 174, 255) : p.pink;
        centered(canvas, nk_rect(bounds.x, bounds.y + title_y, bounds.w, 28), title,
            font, title_color);
    }
    if (hover) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_POINTER);
    if (title_clicked) *title_clicked = hover &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
    return false;
}
bool bongo_cat_ui_content_header(struct nk_context *context,
    const char *title, int icon, bool interactive, bool dark,
    bool native_chrome) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, BONGO_CAT_UI_HEADER_HEIGHT, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return false;
    BongoCatUIPalette p = bongo_cat_ui_palette(dark);
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 0,
        bongo_cat_ui_color_mix(p.surface_glass, p.surface, .2f));
    struct nk_rect icon_bounds = nk_rect(bounds.x + 20, bounds.y + 16, 22, 22);
    if (external_icon_draw) external_icon_draw(external_icon_userdata, canvas,
        icon, icon_bounds, p.accent);
    else bongo_cat_ui_fallback_icon(canvas, icon, icon_bounds, p.accent);
    const struct nk_user_font *font = bongo_cat_ui_label_font(context);
    struct nk_rect text = nk_rect(bounds.x + 52,
        bounds.y + (bounds.h - font->height) * .5f, bounds.w - 110, font->height);
    nk_draw_text(canvas, text, title, nk_strlen(title), font,
        nk_rgba(0, 0, 0, 0), p.accent);
    if (native_chrome) return false;
    struct nk_rect close = nk_rect(bounds.x + bounds.w - 48, bounds.y + 11, 34, 34);
    return bongo_cat_ui_close_button(context, canvas, close, p.muted,
        p.accent, interactive);
}

bool bongo_cat_ui_close_button(struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds,
    struct nk_color normal, struct nk_color hover, bool interactive) {
    bool is_hovered = interactive && nk_input_is_mouse_hovering_rect(
        &context->input, bounds);
    struct nk_color color = is_hovered ? hover : normal;
    float icon_size = NK_MAX(0.0f, NK_MIN(bounds.w, bounds.h) - 14.0f);
    struct nk_rect icon = nk_rect(bounds.x + (bounds.w - icon_size) * .5f,
        bounds.y + (bounds.h - icon_size) * .5f, icon_size, icon_size);
    if (!bongo_cat_ui_draw_icon(canvas, BONGO_CAT_UI_ICON_CLOSE, icon, color)) {
        float inset = icon_size * .22f;
        nk_stroke_line(canvas, icon.x + inset, icon.y + inset,
            icon.x + icon.w - inset, icon.y + icon.h - inset, 2, color);
        nk_stroke_line(canvas, icon.x + icon.w - inset, icon.y + inset,
            icon.x + inset, icon.y + icon.h - inset, 2, color);
    }
    if (is_hovered) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_POINTER);
    return is_hovered && nk_input_is_mouse_click_in_rect(&context->input,
        NK_BUTTON_LEFT, bounds);
}

bool bongo_cat_ui_close_hit(float x, float y, float width) {
    return x >= width - 58.0f && x <= width - 12.0f && y >= 14.0f && y <= 62.0f;
}

bool bongo_cat_ui_title_link_hit(float x, float y, float width) {
    return x >= 8.0f && x <= 8.0f + bongo_cat_ui_sidebar_width(width) &&
        y >= 8.0f && y <= 156.0f;
}

bool bongo_cat_ui_title_drag_hit(float x, float y, float width) {
    float side = bongo_cat_ui_sidebar_width(width);
    return x >= 8.0f + side && x <= width - 8.0f &&
        y >= 8.0f && y <= 8.0f + BONGO_CAT_UI_HEADER_HEIGHT &&
        !bongo_cat_ui_close_hit(x, y, width);
}
