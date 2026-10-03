#include "ui_catime.h"
#include "ui_backend.h"

#ifdef _WIN32
#include <windows.h>
#endif

static struct nk_color rgb(int value) {
    return nk_rgba((value >> 16) & 255, (value >> 8) & 255, value & 255, 255);
}

#ifdef _WIN32
static bool high_contrast(BongoCatUIPalette *value) {
    HIGHCONTRASTW contrast = {0}; contrast.cbSize = sizeof(contrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast),
        &contrast, 0) || !(contrast.dwFlags & HCF_HIGHCONTRASTON)) return false;
    COLORREF window = GetSysColor(COLOR_WINDOW);
    COLORREF text = GetSysColor(COLOR_WINDOWTEXT);
    COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
    #define NK_SYS(c) nk_rgb(GetRValue(c), GetGValue(c), GetBValue(c))
    value->background = value->surface = value->surface_glass =
        value->field = NK_SYS(window);
    value->border = value->border_subtle = value->text = value->muted =
        NK_SYS(text);
    value->accent = value->accent_hover = value->accent_pressed = NK_SYS(accent);
    value->pink = value->pink_hover = NK_SYS(accent);
    value->hover = value->selection = NK_SYS(accent);
    value->hover_pink = NK_SYS(accent);
    value->danger = NK_SYS(GetSysColor(COLOR_HIGHLIGHTTEXT));
    value->danger_background = NK_SYS(accent);
    value->effects = false;
    #undef NK_SYS
    return true;
}
#endif

BongoCatUIPalette bongo_cat_ui_palette(bool dark) {
    BongoCatUIPalette value;
#ifdef _WIN32
    if (high_contrast(&value)) return value;
#endif
    value.background = rgb(dark ? 0x010409 : 0xF0F4F9);
    value.surface = rgb(dark ? 0x010409 : 0xFFFFFF);
    value.surface_glass = rgb(dark ? 0x010409 : 0xFBFCFE);
    value.field = rgb(dark ? 0x2A2E37 : 0xF5F8FC);
    value.border = rgb(dark ? 0x345473 : 0xDAEDFE);
    value.border_subtle = rgb(dark ? 0x343A45 : 0xE8EBF1);
    value.text = rgb(dark ? 0xF4F7FB : 0x182230);
    value.muted = rgb(dark ? 0xA9B2C0 : 0x6D7888);
    value.accent = rgb(0x54AEFF);
    value.accent_hover = rgb(0x3C9AE8);
    value.accent_pressed = rgb(0x2587CF);
    value.pink = rgb(0xF77DAA);
    value.pink_hover = rgb(0xED6F9D);
    value.hover = rgb(dark ? 0x292E37 : 0xEFF7FE);
    value.hover_pink = rgb(dark ? 0x3B2933 : 0xFBEFF6);
    value.selection = rgb(dark ? 0x27384B : 0xE7F3FE);
    value.danger = rgb(dark ? 0xFFA4A4 : 0xFF4D6D);
    value.danger_background = rgb(dark ? 0x4B2B31 : 0xFFECF0);
    value.effects = true;
    return value;
}

bool bongo_cat_ui_dark(const struct nk_context *context) {
    const BongoCatUIBackend *ui = bongo_cat_ui_backend_for_context(context);
    return ui ? ui->dark_theme :
        (context && context->style.window.background.r < 128);
}

static void geometry(struct nk_style *style) {
    style->window.padding = nk_vec2(18, 14);
    style->window.group_padding = nk_vec2(18, 12);
    style->window.spacing = nk_vec2(10, 10);
    style->window.scrollbar_size = nk_vec2(10, 10);
    style->window.border = 0;
    style->window.group_border = 0;
    style->window.rounding = 12;
    style->button.border = 1;
    style->button.rounding = 10;
    style->button.padding = nk_vec2(14, 8);
    style->edit.border = 1;
    style->edit.rounding = 10;
    style->edit.padding = nk_vec2(12, 8);
    style->combo.border = 1;
    style->combo.rounding = 10;
    style->combo.content_padding = nk_vec2(12, 8);
    style->combo.button.rounding = 8;
    style->selectable.rounding = 9;
    style->scrollv.rounding = 3;
    style->scrollv.rounding_cursor = 3;
    style->scrollv.padding = nk_vec2(2, 2);
    style->scrollh = style->scrollv;
}

void bongo_cat_ui_apply_theme(struct nk_context *context, bool dark) {
    struct nk_color table[NK_COLOR_COUNT];
    BongoCatUIPalette p = bongo_cat_ui_palette(dark);
    table[NK_COLOR_TEXT] = p.text;
    table[NK_COLOR_WINDOW] = p.background;
    table[NK_COLOR_HEADER] = p.surface;
    table[NK_COLOR_BORDER] = p.border_subtle;
    table[NK_COLOR_BUTTON] = p.accent;
    table[NK_COLOR_BUTTON_HOVER] = p.accent_hover;
    table[NK_COLOR_BUTTON_ACTIVE] = p.accent_pressed;
    table[NK_COLOR_TOGGLE] = p.field;
    table[NK_COLOR_TOGGLE_HOVER] = p.border_subtle;
    table[NK_COLOR_TOGGLE_CURSOR] = p.accent;
    table[NK_COLOR_SELECT] = p.field;
    table[NK_COLOR_SELECT_ACTIVE] = p.selection;
    table[NK_COLOR_SLIDER] = p.field;
    table[NK_COLOR_SLIDER_CURSOR] = p.accent;
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = p.accent_hover;
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = p.accent_pressed;
    table[NK_COLOR_PROPERTY] = p.field;
    table[NK_COLOR_EDIT] = p.field;
    table[NK_COLOR_EDIT_CURSOR] = p.text;
    table[NK_COLOR_COMBO] = p.field;
    table[NK_COLOR_CHART] = p.surface;
    table[NK_COLOR_CHART_COLOR] = p.accent;
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = p.accent_hover;
    table[NK_COLOR_SCROLLBAR] = p.background;
    table[NK_COLOR_SCROLLBAR_CURSOR] = p.border_subtle;
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = p.accent;
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = p.accent;
    table[NK_COLOR_TAB_HEADER] = p.surface;
    table[NK_COLOR_KNOB] = p.field;
    table[NK_COLOR_KNOB_CURSOR] = p.accent;
    table[NK_COLOR_KNOB_CURSOR_HOVER] = p.accent_hover;
    table[NK_COLOR_KNOB_CURSOR_ACTIVE] = p.accent_pressed;
    nk_style_from_table(context, table);
    geometry(&context->style);
    context->style.button.text_normal = nk_rgb(255, 255, 255);
    context->style.button.text_hover = nk_rgb(255, 255, 255);
    context->style.button.text_active = nk_rgb(255, 255, 255);
    context->style.window.fixed_background = nk_style_item_color(p.background);
    context->style.window.group_border_color = p.border_subtle;
    context->style.text.color = p.text;
    BongoCatUIBackend *ui = bongo_cat_ui_backend_for_context(context);
    if (ui) ui->dark_theme = dark;
}
