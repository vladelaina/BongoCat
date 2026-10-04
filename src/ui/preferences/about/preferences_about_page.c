#include "preferences_about_internal.h"
#include "preferences_about_community.h"
#include "preferences_state.h"

void bongo_cat_preferences_page_about(BongoCatPreferences *value,
    struct nk_context *context) {
    bongo_cat_preferences_support_assets_load(value);
    bongo_cat_preferences_about_hero(value, context);
    bongo_cat_preferences_about_community(value, context);
    bongo_cat_about_contributors(value, context);
    bongo_cat_preferences_about_projects(value, context);
}
