#include "preferences_about_internal.h"
#include "preferences_state.h"
#include "preferences_about_community.h"
#include "preferences_about_footer.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_animation.h"
#include "ui_paint.h"
#include "ui_icons.h"

#include <SDL3/SDL.h>
#include <stdio.h>
static const char *tr(BongoCatPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(value->app->i18n, key, fallback);
}
static float width(const struct nk_user_font *font, const char *value) {
    return font->width(font->userdata, font->height, value, nk_strlen(value));
}
static void text(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}
static void centered_span(struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value, int length,
    const struct nk_user_font *font, struct nk_color color) {
    float value_width = font->width(font->userdata, font->height, value, length);
    float target_width = NK_MIN(bounds.w, value_width + 1.0f);
    nk_draw_text(canvas, nk_rect(bounds.x + (bounds.w - target_width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f, target_width,
        font->height), value, length, font, nk_rgba(0, 0, 0, 0), color);
}
static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    centered_span(canvas, bounds, value, nk_strlen(value), font, color);
}
static void centered_wrapped(struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value,
    const struct nk_user_font *font, struct nk_color color) {
    int length = nk_strlen(value), split = -1; float best = 1.0e30f;
    if (width(font, value) <= bounds.w) {
        centered_span(canvas, bounds, value, length, font, color); return;
    }
    for (int i = 1; i + 1 < length; ++i) {
        if (value[i] != ' ') continue;
        float left = font->width(font->userdata, font->height, value, i);
        float right = font->width(font->userdata, font->height,
            value + i + 1, length - i - 1);
        float widest = NK_MAX(left, right);
        if (widest < best) { best = widest; split = i; }
    }
    if (split < 0) { centered(canvas, bounds, value, font, color); return; }
    struct nk_rect line = nk_rect(bounds.x,
        bounds.y + (bounds.h - font->height * 2) * .5f, bounds.w, font->height);
    centered_span(canvas, line, value, split, font, color); line.y += font->height;
    centered_span(canvas, line, value + split + 1, length - split - 1, font, color);
}
static bool hit(struct nk_context *context, struct nk_rect bounds) {
    return nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
}
static void link_cursor(struct nk_context *context, struct nk_rect bounds) {
    if (nk_input_is_mouse_hovering_rect(&context->input, bounds))
        bongo_cat_ui_cursor_hover_rect(context, bounds,
            BONGO_CAT_UI_CURSOR_POINTER);
}
static void open_url(const char *url) {
    if (!SDL_OpenURL(url)) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Cannot open URL: %s", SDL_GetError());
}
static void image_contain(struct nk_command_buffer *canvas, unsigned int texture,
    int image_width, int image_height, struct nk_rect bounds) {
    if (!texture || image_width < 1 || image_height < 1) return;
    float scale = NK_MIN(bounds.w / image_width, bounds.h / image_height);
    struct nk_rect target = nk_rect(bounds.x + (bounds.w - image_width * scale) * .5f,
        bounds.y + (bounds.h - image_height * scale) * .5f,
        image_width * scale, image_height * scale);
    struct nk_image image = nk_image_id((int)texture);
    nk_draw_image(canvas, target, &image, nk_rgb(255, 255, 255));
}
static void support_logo(BongoCatPreferences *value,
    struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds,
    BongoCatUIPalette p) {
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    float lift = bongo_cat_ui_animate_eased(context, "support-logo-hover",
        hover ? 1.0f : 0.0f, 250.0f, BONGO_CAT_UI_EASE_SPRING);
    float scale = 1.0f + .04f * lift;
    struct nk_rect raised = nk_rect(
        bounds.x + (bounds.w - bounds.w * scale) * .5f,
        bounds.y - 5.0f * lift + (bounds.h - bounds.h * scale) * .5f,
        bounds.w * scale, bounds.h * scale);
    /* The logo asset is the complete brand mark; avoid wrapping it in a
       second card so the support page and sidebar share the same identity. */
    if (p.effects) bongo_cat_ui_paint_shadow(context, raised, 42, 0, 10, 24, 0,
        nk_rgba(p.pink.r, p.pink.g, p.pink.b, 70));
    image_contain(canvas, value->logo_texture, value->logo_width,
        value->logo_height, raised);
    link_cursor(context, bounds);
    if (hit(context, bounds)) open_url("https://bongocat.pet");
}
static void star_button(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, BongoCatUIPalette p) {
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    if (hover) bounds.y -= 2;
    if (p.effects) {
        bongo_cat_ui_paint_shadow(context, bounds, 24, 0, 10, 26, 0,
            nk_rgba(p.pink.r, p.pink.g, p.pink.b, hover ? 97 : 77));
        bongo_cat_ui_paint_gradient(context, bounds, 24,
            hover ? nk_rgb(255, 141, 184) : nk_rgb(255, 155, 193),
            hover ? nk_rgb(237, 103, 154) : nk_rgb(244, 119, 168));
    } else nk_fill_rect(canvas, bounds, 23, hover ? p.pink_hover : p.pink);
    nk_stroke_rect(canvas, bounds, 24, 1, nk_rgba(255, 255, 255, 133));
    bongo_cat_preferences_icon_draw(value, canvas,
        BONGO_CAT_UI_ICON_GITHUB,
        nk_rect(bounds.x + 22, bounds.y + 14.5f, 19, 19), nk_rgb(255, 255, 255));
    text(canvas, nk_rect(bounds.x + 50, bounds.y +
        (bounds.h - value->ui.label_font->height) * .5f,
        bounds.w - 62, value->ui.label_font->height), tr(value,
        "native.support.starOnGitHub", "Star on GitHub!"),
        value->ui.label_font, nk_rgb(255, 255, 255));
    link_cursor(context, bounds);
    if (hit(context, bounds))
        open_url("https://github.com/vladelaina/BongoCat");
}
static void hero_title(BongoCatPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds,
    BongoCatUIPalette p) {
    const char *title = "BongoCat";
    const char *by = tr(value, "native.support.by", "by ");
    const char *developer = "vladelaina";
    float gap = 8, title_width = width(value->ui.hero_font, title);
    float by_width = width(value->ui.caption_font, by),
        developer_width = width(value->ui.caption_font, developer);
    float x = bounds.x + (bounds.w - title_width - by_width -
        developer_width - gap) * .5f;
    text(canvas, nk_rect(x, bounds.y, title_width + 1, 32), title,
        value->ui.hero_font, p.text);
    text(canvas, nk_rect(x + title_width + gap, bounds.y + 8,
        by_width + 1, 22), by, value->ui.caption_font, p.muted);
    struct nk_rect link = nk_rect(x + title_width + gap + by_width,
        bounds.y + 8, developer_width + 1, 22);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, link);
    float amount = bongo_cat_ui_animate_eased(context, "support-author-hover",
        hover ? 1.0f : 0.0f, 200, BONGO_CAT_UI_EASE_STANDARD);
    text(canvas, link, developer, value->ui.caption_font,
        bongo_cat_ui_color_mix(p.accent, p.pink, amount));
    link_cursor(context, link); if (hit(context, link))
        open_url("https://vladelaina.com");
}
void bongo_cat_preferences_about_hero(BongoCatPreferences *value,
    struct nk_context *context) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 340, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    if (p.effects) {
        nk_push_scissor(canvas, bounds);
        bongo_cat_ui_paint_radial_circle(context,
            nk_rect(bounds.x + bounds.w * .5f - 300,
            bounds.y - 190, 600, 360),
            nk_rgba(p.accent.r, p.accent.g, p.accent.b, 41),
            nk_rgba(p.pink.r, p.pink.g, p.pink.b, 20), .49f, .84f);
        nk_push_scissor(canvas, nk_window_get_content_region(context));
    }
    star_button(value, context, canvas,
        nk_rect(bounds.x + bounds.w - 188, bounds.y + 6, 177, 46), p);
    support_logo(value, context, canvas,
        nk_rect(bounds.x + (bounds.w - 168) * .5f + 3,
        bounds.y + 10, 168, 168), p);
    hero_title(value, context, canvas, nk_rect(bounds.x + 3, bounds.y + 190,
        bounds.w, 36), p);
    bongo_cat_preferences_about_localized_link(value, context, canvas,
        nk_rect(bounds.x, bounds.y + 226, bounds.w, 24),
        "native.support.website", "Website: bongocat.pet",
        "https://bongocat.pet", "support-website-hover", p);
    centered_wrapped(canvas, nk_rect(bounds.x + 36, bounds.y + 254,
        bounds.w - 72, 40),
        tr(value, "native.support.heroText",
        "Thank you for your support. Every use and share helps BongoCat grow."),
        value->ui.caption_font, p.muted);
    bongo_cat_preferences_about_footer(value, context, canvas,
        nk_rect(bounds.x, bounds.y + 300, bounds.w, 34), p);
}
static void project(BongoCatPreferences *value, struct nk_context *context,
    struct nk_command_buffer *canvas, struct nk_rect bounds, unsigned int texture,
    int image_width, int image_height, const char *name, const char *url,
    bool pink, BongoCatUIPalette p) {
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    char animation_id[48];
    snprintf(animation_id, sizeof(animation_id), "project-hover-%s", name);
    float lift = bongo_cat_ui_animate_eased(context, animation_id,
        hover ? 1.0f : 0.0f, 280.0f, BONGO_CAT_UI_EASE_SWIFT);
    struct nk_rect icon = nk_rect(bounds.x + (bounds.w - 148) * .5f,
        bounds.y - 7.0f * lift, 148, 148);
    if (p.effects) {
        bongo_cat_ui_paint_radial(context,
            nk_rect(icon.x - 16, icon.y - 16, icon.w + 32, icon.h + 32),
            nk_rgba(pink ? p.pink.r : p.accent.r,
                pink ? p.pink.g : p.accent.g, pink ? p.pink.b : p.accent.b, 43),
            nk_rgba(0, 0, 0, 0), .05f, 1.0f);
        bongo_cat_ui_paint_shadow(context, icon, 37, 0, 18, 34, 0,
            nk_rgba(p.text.r, p.text.g, p.text.b, 38));
    }
    if (!pink && p.effects) bongo_cat_ui_paint_gradient(context, icon, 37,
        nk_rgb(245, 241, 255), nk_rgb(234, 247, 255));
    else nk_fill_rect(canvas, icon, 37, pink ? p.surface : p.selection);
    image_contain(canvas, texture, image_width, image_height, pink ? icon :
        nk_rect(icon.x + 5, icon.y + 8, icon.w - 10, icon.h - 16));
    centered(canvas, nk_rect(bounds.x, bounds.y + 166, bounds.w, 30), name,
        value->ui.heading_font, hover ? (pink ? p.pink : p.accent) : p.text);
    link_cursor(context, bounds);
    if (hit(context, bounds)) open_url(url);
}

void bongo_cat_preferences_about_projects(BongoCatPreferences *value,
    struct nk_context *context) {
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, 330, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    bongo_cat_preferences_about_projects_heading(value, context, bounds);
    float card_width = 220, center = bounds.x + bounds.w * .5f + 3;
    project(value, context, canvas, nk_rect(center - 262, bounds.y + 112,
        card_width, 200), value->catime_texture, value->catime_width,
        value->catime_height, "Catime", "https://cati.me/", false, p);
    project(value, context, canvas, nk_rect(center + 42, bounds.y + 112,
        card_width, 200), value->vlaina_texture, value->vlaina_width,
        value->vlaina_height, "vlaina", "https://vlaina.com/r/bongocat",
        true, p);
}
