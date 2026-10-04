#include "preferences_about_internal.h"
#include "preferences_about_community.h"
#include "preferences_state.h"
#include "ui_animation.h"
#include "ui_backend.h"
#include "ui_catime.h"
#include "ui_icons.h"
#include "ui_paint.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static const char *tr(BongoCatPreferences *value, const char *key,
    const char *fallback) {
    return bongo_cat_i18n_get(value->app->i18n, key, fallback);
}

static void text(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    nk_draw_text(canvas, bounds, value, nk_strlen(value), font,
        nk_rgba(0, 0, 0, 0), color);
}

static void centered_span(struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *value, int length,
    const struct nk_user_font *font, struct nk_color color) {
    float width = font->width(font->userdata, font->height,
        value, length);
    nk_draw_text(canvas, nk_rect(bounds.x + (bounds.w - width) * .5f,
        bounds.y + (bounds.h - font->height) * .5f,
        NK_MIN(width + 1, bounds.w), font->height), value, length, font,
        nk_rgba(0, 0, 0, 0), color);
}

static void centered(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, const struct nk_user_font *font, struct nk_color color) {
    centered_span(canvas, bounds, value, nk_strlen(value), font, color);
}

static float span_width(const struct nk_user_font *font,
    const char *value, int length) {
    return font->width(font->userdata, font->height, value, length);
}

static void span(struct nk_command_buffer *canvas, struct nk_rect bounds,
    const char *value, int length, const struct nk_user_font *font,
    struct nk_color color) {
    if (length > 0) nk_draw_text(canvas, bounds, value, length, font,
        nk_rgba(0, 0, 0, 0), color);
}

static bool hit(struct nk_context *context, struct nk_rect bounds) {
    return nk_input_is_mouse_hovering_rect(&context->input, bounds) &&
        nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, bounds);
}

static void open_url(const char *url) {
    if (!SDL_OpenURL(url)) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Cannot open URL: %s", SDL_GetError());
}

/* Localized labels feed the font atlas and must not be hardcoded here. */
void bongo_cat_preferences_about_localized_link(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, const char *key, const char *fallback,
    const char *url, const char *animation_id, BongoCatUIPalette palette) {
    const char *label = tr(value, key, fallback);
    const struct nk_user_font *font = value->ui.caption_font;
    float text_width = font->width(font->userdata, font->height,
        label, nk_strlen(label));
    struct nk_rect link = nk_rect(bounds.x + (bounds.w - text_width) * .5f,
        bounds.y, text_width + 2, bounds.h);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, link);
    float amount = bongo_cat_ui_animate_eased(context, animation_id,
        hover ? 1.0f : 0.0f, 200, BONGO_CAT_UI_EASE_STANDARD);
    centered(canvas, link, label, font,
        bongo_cat_ui_color_mix(palette.pink, palette.accent, amount));
    if (hover) bongo_cat_ui_cursor_hover_rect(context, link,
        BONGO_CAT_UI_CURSOR_POINTER);
    if (hit(context, link)) open_url(url);
}

static void community_link(BongoCatPreferences *value,
    struct nk_context *context, struct nk_command_buffer *canvas,
    struct nk_rect bounds, int index, BongoCatUIPalette p) {
    const char *labels[] = {"Discord", tr(value,
        "native.support.qqGroup", "QQ Group"), tr(value,"native.about.wechat","WeChat")};
    const char *details[] = {"https://discord.gg/vf8jqnattk", "422616922",
        tr(value,"native.about.wechatHint","Hover to scan and chat")};
    const char *urls[] = {"https://discord.gg/vf8jqnattk",
        "https://qm.qq.com/q/6ksRMCZIuA"};
    struct nk_color brand = index == 2 ? nk_rgb(7,193,96) : index ? p.accent : nk_rgb(88, 101, 242);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, canvas->clip) &&
        nk_input_is_mouse_hovering_rect(&context->input, bounds);
    char id[32]; snprintf(id, sizeof(id), "community-hover-%d", index);
    float lift = bongo_cat_ui_animate_eased(context, id,
        hover ? 1.0f : 0.0f, 220.0f, BONGO_CAT_UI_EASE_SWIFT);
    bounds.y -= 2.0f * lift;
    const struct nk_color light[] = {nk_rgb(242, 244, 253), nk_rgb(242, 248, 254), nk_rgb(237, 248, 241)};
    struct nk_color background = bongo_cat_ui_dark(context)
        ? bongo_cat_ui_color_mix(p.surface_glass, brand, .055f) : light[index];
    if (hover && p.effects) bongo_cat_ui_paint_shadow(context, bounds, 18,
        0, 10, 24, 0, nk_rgba(brand.r, brand.g, brand.b, 28));
    nk_fill_rect(canvas, bounds, 16, background);
    struct nk_rect mark = nk_rect(bounds.x + 12, bounds.y + (bounds.h-36)*.5f, 36, 36);
    nk_fill_rect(canvas, mark, 12, brand);
    if(index == 2) {
        bongo_cat_about_wechat_icon(value, context, nk_rect(mark.x + 5.5f, mark.y + 5.5f, 25, 25));
    } else bongo_cat_preferences_icon_draw(value, canvas,
        index ? BONGO_CAT_UI_ICON_QQ : BONGO_CAT_UI_ICON_DISCORD,
        nk_rect(mark.x + 5.5f, mark.y + 5.5f, 25, 25), nk_rgb(255, 255, 255));
    text(canvas, nk_rect(bounds.x + 57, bounds.y + bounds.h*.5f-21, bounds.w - 77, 22),
        labels[index], bongo_cat_about_font(value, 20), p.text);
    const struct nk_user_font *detail_font = bongo_cat_about_font(value, 14.0f);
    char detail[256];
    snprintf(detail, sizeof(detail), "%s", details[index]);
    int length = nk_strlen(detail);
    float detail_width = NK_MAX(0.0f, bounds.w - 80.0f);
    if (detail_font->width(detail_font->userdata, detail_font->height, detail, length) > detail_width) {
        float dots = detail_font->width(detail_font->userdata, detail_font->height, "...", 3);
        while (length && detail_font->width(detail_font->userdata, detail_font->height, detail, length) + dots > detail_width) {
            length--;
            while (length && ((unsigned char)detail[length] & 0xc0) == 0x80) length--;
        }
        snprintf(detail + length, sizeof(detail) - (size_t)length, "...");
    }
    text(canvas, nk_rect(bounds.x + 57, bounds.y + bounds.h*.5f+3, detail_width, 18),
        detail, detail_font, p.muted);
    float right = bounds.x + bounds.w - (bounds.h < 80 ? 20.0f : 10.0f);
    float middle = bounds.y + bounds.h * .5f;
    nk_stroke_line(canvas, right - 5, middle - 3, right, middle, 1.5f, brand);
    nk_stroke_line(canvas, right, middle, right - 5, middle + 3, 1.5f, brand);
    if (hover) bongo_cat_ui_cursor_hover_rect(context, bounds,
        BONGO_CAT_UI_CURSOR_POINTER);
    if(index == 2) {
        if(hover) bongo_cat_about_wechat(value,bounds);
    } else if (hit(context, bounds)) open_url(urls[index]);
}

void bongo_cat_preferences_about_projects_heading(
    BongoCatPreferences *value, struct nk_context *context,
    struct nk_rect bounds) {
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    centered(canvas, nk_rect(bounds.x, bounds.y + 20, bounds.w, 30),
        tr(value, "native.support.works", "More apps"),
        value->ui.heading_font, p.text);
    const char *caption = tr(value, "native.support.worksText",
        "More software from vladelaina");
    static const char developer_name[] = "vladelaina";
    const char *developer = strstr(caption, developer_name);
    int prefix = developer ? (int)(developer - caption) : nk_strlen(caption);
    int name_length = developer ? (int)(sizeof(developer_name) - 1) : 0;
    const char *suffix = developer ? developer + name_length : caption + prefix;
    int suffix_length = nk_strlen(suffix);
    float prefix_width = span_width(value->ui.caption_font, caption, prefix);
    float name_width = span_width(value->ui.caption_font,
        developer ? developer : "", name_length);
    float suffix_width = span_width(value->ui.caption_font, suffix, suffix_length);
    float x = bounds.x + (bounds.w - prefix_width - name_width - suffix_width) * .5f;
    float y = bounds.y + 49;
    span(canvas, nk_rect(x, y, prefix_width + 1, 24), caption, prefix,
        value->ui.caption_font, p.muted);
    struct nk_rect link = nk_rect(x + prefix_width, y, name_width + 1, 24);
    bool hover = developer && nk_input_is_mouse_hovering_rect(
        &context->input, link);
    float amount = bongo_cat_ui_animate_eased(context, "works-author-hover",
        hover ? 1.0f : 0.0f, 200, BONGO_CAT_UI_EASE_STANDARD);
    span(canvas, link, developer ? developer : "", name_length,
        value->ui.caption_font, bongo_cat_ui_color_mix(p.accent, p.pink, amount));
    span(canvas, nk_rect(link.x + name_width, y, suffix_width + 1, 24),
        suffix, suffix_length, value->ui.caption_font, p.muted);
    if (hover) bongo_cat_ui_cursor_hover_rect(context, link,
        BONGO_CAT_UI_CURSOR_POINTER);
    if (developer && hit(context, link))
        open_url("https://vladelaina.com");
}

void bongo_cat_preferences_about_community(
    BongoCatPreferences *value, struct nk_context *context) {
    struct nk_rect bounds;
    float viewport, height;
    bongo_cat_ui_logical_size(&value->ui, &viewport, &height);
    (void)height;
    bool stacked = viewport <= 780.0f;
    nk_layout_row_dynamic(context, stacked ? 334.0f : 194.0f, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    BongoCatUIPalette p = bongo_cat_ui_palette(
        bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    centered(canvas, nk_rect(bounds.x, bounds.y + 24, bounds.w, 30),
        tr(value, "native.support.community", "Community"),
        bongo_cat_about_font(value, 24), p.text);
    for (int i = 0; i < 3; ++i)
        community_link(value, context, canvas,
            nk_rect(bounds.x + (stacked?0:i*(bounds.w+12)/3),
            bounds.y + 74 + (stacked?i*80:0), stacked?bounds.w:(bounds.w-24)/3,
            stacked?68.0f:88.0f), i, p);
}
