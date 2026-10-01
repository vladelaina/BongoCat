#include "preferences_state.h"
#include "preferences_about_internal.h"
#include <SDL3/SDL_opengl.h>
#include "ui_catime.h"
#include "ui_animation.h"
#include "ui_paint.h"
#include "ui_icons.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void contributors_load(BongoCatPreferences *value) {
    BongoCatAboutState *s = &value->about;
    bongo_cat_about_refresh(value);
    if (s->contributors && !s->portraits_loaded) {
        if (!s->portraits_next) {
            glDeleteTextures(BONGO_ABOUT_CONTRIBUTOR_CAP, s->portraits);
            memset(s->portraits, 0, sizeof(s->portraits));
        }
        /* Bound GL work per frame, including the maximum-size contributor list. */
        int end = NK_MIN(s->portraits_next + 4, s->contributors->count);
        for (int i = s->portraits_next; i < end; i++) {
            unsigned char *pixels = s->contributors->people[i].pixels;
            if (!pixels) continue;
            glGenTextures(1, &s->portraits[i]);
            glBindTexture(GL_TEXTURE_2D, s->portraits[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BONGO_ABOUT_AVATAR_SIZE,
                BONGO_ABOUT_AVATAR_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        s->portraits_next = end;
        s->portraits_loaded = end == s->contributors->count;
        if (!s->portraits_loaded) value->render_dirty = true;
    }
}

void bongo_cat_about_contributors(BongoCatPreferences *value, struct nk_context *context) {
    contributors_load(value);
    float available = nk_window_get_content_region_size(context).x;
    float viewport, viewport_height;
    bongo_cat_ui_logical_size(&value->ui, &viewport, &viewport_height);
    (void)viewport_height;
    bool compact = viewport <= 560.0f;
    float inset = compact ? 8.0f : 16.0f;
    float avatar_size = compact ? 54.0f : 64.0f;
    float gap = compact ? 18.0f : 24.0f;
    float stride = avatar_size + gap;
    float inner = available - 2 * inset;
    const struct nk_user_font *body_font = bongo_cat_about_font(value, 16);
    const struct nk_user_font *tag_font = bongo_cat_about_font(value, 14);
    const struct nk_user_font *join_font = bongo_cat_about_font(value, compact ? 23.0f : 24.0f);
    const char *description = bongo_cat_i18n_get(value->app->i18n, "native.about.contributorsText",
        "Every step of BongoCat comes from open source. Thank you to all our contributors.");
    const char *join_title = bongo_cat_i18n_get(value->app->i18n, "native.about.joinTitle",
        "Want to appear in the contributors list?");
    const char *join_text = bongo_cat_i18n_get(value->app->i18n, "native.about.joinText",
        "Code, documentation, bug reports and translations: every contribution makes BongoCat better.");
    const char *join_button = bongo_cat_i18n_get(value->app->i18n, "native.about.joinGithub", "Contribute on GitHub");
    float natural_button_width = NK_MAX(220.0f, body_font->width(body_font->userdata,
        body_font->height, join_button, nk_strlen(join_button)) + 80.0f);
    bool wide = viewport > 768.0f && inner >= 344.0f + natural_button_width;
    bool centered = viewport <= 768.0f;
    float content_width = wide ? inner - natural_button_width - 24.0f : inner;
    float title_height = compact ? 31.25f : 37.5f;
    float desc_y = 44.0f + title_height + 16.0f;
    float grid_y = desc_y + bongo_cat_about_paragraph(NULL, nk_rect(0, 0, inner, 0),
        description, body_font, nk_rgb(0, 0, 0), true, 28.8f) + 32.0f;
    int columns = NK_MAX(1, (int)((inner + gap) / stride));
    int total = value->about.contributors ? value->about.contributors->count : 0;
    int rows = (NK_MAX(1, total) + columns - 1) / columns;
    float grid_height = rows * stride - gap;
    float join_y = grid_y + grid_height + 32.0f;
    float heading_y = 28.0f + 13.0f + 10.4f;
    float paragraph_y = heading_y + bongo_cat_about_text_height(join_title, join_font, content_width) + 6.0f;
    float tags_y = paragraph_y + bongo_cat_about_paragraph(NULL, nk_rect(0, 0, content_width, 0),
        join_text, body_font, nk_rgb(0, 0, 0), centered, 24.8f) + 18.0f;
    const char *keys[] = {"native.about.code", "native.about.docs", "native.about.bugs", "native.about.translations"};
    const char *fallbacks[] = {"Code", "Documentation", "Bug reports", "Translations"};
    const char *marks[] = {"〈/〉", "▤", "♧", "文"};
    float mark_widths[4];
    const char *labels[4];
    struct nk_rect tags[4];
    float tag_x = 0, tag_y = tags_y;
    for (int i = 0; i < 4; i++) {
        labels[i] = bongo_cat_i18n_get(value->app->i18n, keys[i], fallbacks[i]);
        mark_widths[i] = tag_font->width(tag_font->userdata, tag_font->height, marks[i], nk_strlen(marks[i]));
        float tw = NK_MIN(content_width, tag_font->width(tag_font->userdata, tag_font->height,
            labels[i], nk_strlen(labels[i])) + mark_widths[i] + 26.4f);
        if (tag_x && tag_x + tw > content_width) { tag_x = 0; tag_y += 40.0f; }
        tags[i] = nk_rect(tag_x, tag_y, tw, 32);
        tag_x += tw + 8.0f;
    }
    if (centered) {
        for (int first = 0; first < 4;) {
            int last = first;
            while (last + 1 < 4 && tags[last + 1].y == tags[first].y) last++;
            float shift = (content_width - tags[last].x - tags[last].w) * .5f;
            for (int i = first; i <= last; i++) tags[i].x += shift;
            first = last + 1;
        }
    }
    float content_height = tag_y + 32.0f;
    float button_y = wide ? 28.0f + (content_height - 28.0f - 46.0f) * .5f : content_height + 24.0f;
    struct nk_rect bounds;
    nk_layout_row_dynamic(context, join_y + NK_MAX(content_height, button_y + 46.0f) + 44.0f, 1);
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID)
        return;
    BongoCatUIPalette p = bongo_cat_ui_palette(bongo_cat_ui_dark(context));
    struct nk_command_buffer *canvas = nk_window_get_canvas(context);
    struct nk_rect avatars[BONGO_ABOUT_CONTRIBUTOR_CAP];
    for (int i = 0; i < total; i++) {
        int row = i / columns, count = NK_MIN(columns, total - row * columns);
        avatars[i] = nk_rect(bounds.x + (bounds.w - (count * stride - gap)) * .5f + (i % columns) * stride,
                             bounds.y + grid_y + row * stride, avatar_size, avatar_size);
    }
    value->about.title_font = *value->ui.heading_font;
    value->about.title_font.height = compact ? 25.0f : 30.0f;
    const struct nk_user_font *title_font = &value->about.title_font;
    const char *title = bongo_cat_i18n_get(value->app->i18n, "native.about.contributorsTitle",
                                           "Open source & community contributions");
    int title_length = nk_strlen(title);
    float title_width =
        title_font->width(title_font->userdata, title_font->height, title, title_length);
    if (title_width > bounds.w - 32) {
        value->about.title_font.height *= (bounds.w - 32) / title_width;
        title_width = bounds.w - 32;
    }
    float title_x = bounds.x + (bounds.w - title_width) * .5f;
    float prefix = 0;
    for (int i = 0; i < title_length;) {
        nk_rune rune;
        int bytes = nk_utf_decode(title + i, &rune, title_length - i);
        if (bytes <= 0)
            break;
        float glyph = title_font->width(title_font->userdata, title_font->height, title + i, bytes);
        struct nk_color color =
            bongo_cat_ui_color_mix(p.accent, p.pink, title_width ? prefix / title_width : 0);
        nk_draw_text(canvas,
                     nk_rect(title_x + prefix, bounds.y + 44, glyph + 2, title_font->height),
                     title + i, bytes, title_font, nk_rgba(0, 0, 0, 0), color);
        i += bytes;
        prefix += glyph;
    }
    bongo_cat_about_paragraph(context, nk_rect(bounds.x + inset, bounds.y + desc_y, inner, grid_y - desc_y),
        description, body_font, p.muted, true, 28.8f);
    if (!total) {
        struct nk_rect status = nk_rect(bounds.x + inset, bounds.y + grid_y, inner, avatar_size);
        bool loading = value->about.contributors_request != NULL;
        bongo_cat_about_text(value, context, status,
            loading ? "native.about.loading" : "native.about.contributorsFailed",
            loading ? "Loading..." : "Cannot load contributors. Click to retry.",
            body_font, p.muted, true);
        if (!loading && nk_input_is_mouse_hovering_rect(&context->input, canvas->clip) &&
            nk_input_is_mouse_hovering_rect(&context->input, status)) {
            bongo_cat_ui_cursor_hover_rect(context, status, BONGO_CAT_UI_CURSOR_POINTER);
            if (nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, status)) {
                value->about.contributors_attempted = false;
                value->render_dirty = true;
            }
        }
    }
    for (int i = 0; i < total; i++) {
        struct nk_rect avatar = avatars[i];
        bool hover = nk_input_is_mouse_hovering_rect(&context->input, canvas->clip) &&
                     nk_input_is_mouse_hovering_rect(&context->input, avatar);
        char id[32];
        snprintf(id, sizeof(id), "contributor-%d", i);
        float amount =
            bongo_cat_ui_animate_eased(context, id, hover ? 1.0f : 0.0f, 400, BONGO_CAT_UI_EASE_STANDARD);
        float growth = avatar_size * .075f * amount;
        struct nk_rect raised = nk_rect(avatar.x - growth, avatar.y - growth - 6.0f * amount,
                                        avatar_size + growth * 2, avatar_size + growth * 2);
        if (hover && p.effects)
            bongo_cat_ui_paint_shadow(context, raised, 36, 0, 8, 18, 0, nk_rgba(130, 80, 223, 65));
        nk_fill_circle(canvas, raised, p.field);
        if (value->about.portraits[i]) {
            struct nk_image image = nk_image_id((int)value->about.portraits[i]);
            nk_draw_image(canvas, raised, &image, nk_rgb(255, 255, 255));
        }
        nk_stroke_circle(canvas, raised, 2, bongo_cat_ui_color_mix(nk_rgb(255, 255, 255), p.pink, amount));
        if (hover) {
            bongo_cat_ui_cursor_hover_rect(context, avatar, BONGO_CAT_UI_CURSOR_POINTER);
            const char *profile = value->about.contributors->people[i].profile;
            if (*profile && nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, avatar))
                SDL_OpenURL(profile);
        }
    }
    float x = bounds.x + inset, y = bounds.y + join_y, w = inner;
    for (int i = 0; i < 48; i++) {
        float t = ((float)i + .5f) / 48.0f;
        float alpha = NK_MIN(1.0f, NK_MIN(t, 1.0f - t) / .15f) * .25f;
        struct nk_color c = bongo_cat_ui_color_mix(nk_rgb(130, 80, 223), nk_rgb(30, 150, 235),
            1.0f - fabsf(t * 2.0f - 1.0f));
        nk_fill_rect(canvas, nk_rect(x + w * (float)i / 48.0f, y, w / 48.0f + .5f, 1), 0,
            bongo_cat_ui_color_alpha(c, alpha));
    }
    bongo_cat_about_text(value, context, nk_rect(x, y + 28, content_width, 18), NULL, "OPEN SOURCE",
                         bongo_cat_about_font(value, 13), p.pink, centered);
    bongo_cat_about_text(value, context, nk_rect(x, y + heading_y, content_width, paragraph_y - heading_y),
                         NULL, join_title, join_font, p.text, centered);
    bongo_cat_about_paragraph(context, nk_rect(x, y + paragraph_y, content_width, tags_y - paragraph_y),
                         join_text, body_font, p.muted, centered, 24.8f);
    struct nk_color colors[] = {p.pink, nk_rgb(122, 162, 247), p.pink, nk_rgb(39, 201, 63)};
    for (int i = 0; i < 4; i++) {
        struct nk_rect tag = tags[i]; tag.x += x; tag.y += y;
        nk_fill_rect(canvas, tag, 16, bongo_cat_ui_color_mix(p.surface, colors[i], .05f));
        nk_stroke_rect(canvas, tag, 16, 1, bongo_cat_ui_color_alpha(colors[i], .15f));
        bongo_cat_about_text(value, context, nk_rect(tag.x + 10, tag.y + 9, mark_widths[i] + 1, 16), NULL,
            marks[i], tag_font, colors[i], true);
        bongo_cat_about_text(value, context, nk_rect(tag.x + 16.4f + mark_widths[i], tag.y + 9,
            tag.w - 26.4f - mark_widths[i] + 1, 16), NULL,
                             labels[i], tag_font, colors[i], true);
    }
    float button_width = NK_MIN(w, centered ? NK_MAX(280.0f, natural_button_width) : natural_button_width);
    struct nk_rect button = nk_rect(x + (wide ? w - button_width : centered ? (w - button_width) * .5f : 0),
        y + button_y, button_width, 46);
    bool hover = nk_input_is_mouse_hovering_rect(&context->input, canvas->clip) &&
        nk_input_is_mouse_hovering_rect(&context->input, button);
    float lift = bongo_cat_ui_animate_eased(context, "about-join-hover", hover ? 1.0f : 0.0f,
        400, BONGO_CAT_UI_EASE_STANDARD);
    button.y -= lift * 3.0f;
    if (p.effects) bongo_cat_ui_paint_shadow(context, button, 23, 0, 8, 20, 0,
        bongo_cat_ui_color_alpha(p.pink, .04f + .18f * lift));
    nk_fill_rect(canvas, button, 23, bongo_cat_ui_color_mix(p.surface, p.pink, .08f * lift));
    nk_stroke_rect(canvas, button, 23, 1, bongo_cat_ui_color_alpha(p.pink, .35f + .3f * lift));
    bongo_cat_preferences_icon_draw(value, canvas, BONGO_CAT_UI_ICON_GITHUB,
        nk_rect(button.x + 17, button.y + 13, 20, 20), p.pink);
    bongo_cat_about_text(value, context, nk_rect(button.x + 43, button.y + 16, button.w - 74, 20),
                         "native.about.joinGithub", "Contribute on GitHub", body_font, p.pink, true);
    float arrow = button.x + button.w - 23 + 4 * lift, ay = button.y + 23;
    nk_stroke_line(canvas, arrow - 8, ay, arrow, ay, 1.3f, p.pink);
    nk_stroke_line(canvas, arrow - 4, ay - 4, arrow, ay, 1.3f, p.pink);
    nk_stroke_line(canvas, arrow - 4, ay + 4, arrow, ay, 1.3f, p.pink);
    if (hover) {
        bongo_cat_ui_cursor_hover_rect(context, button, BONGO_CAT_UI_CURSOR_POINTER);
        if (nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_LEFT, button))
            SDL_OpenURL("https://github.com/vladelaina/BongoCat");
    }
}
