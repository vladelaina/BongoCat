#include "preferences_page_dispatch.h"

void bongo_cat_preferences_draw_page(BongoCatPreferences *value,
    struct nk_context *context) {
    switch (value->page) {
    case 0: bongo_cat_preferences_page_settings(value, context); break;
    case 1: bongo_cat_preferences_page_model(value, context); break;
    case 2: bongo_cat_preferences_page_shortcuts(value, context); break;
    default: bongo_cat_preferences_page_about(value, context); break;
    }
}
