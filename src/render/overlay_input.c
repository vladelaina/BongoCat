#include "overlay_internal.h"
#include "bongo_cat/path.h"

#include <stdio.h>
#include <string.h>

static bool key_path(BongoCatOverlay *value, const char *group, const char *name,
    char path[BONGO_CAT_PATH_CAP]) {
    char relative[BONGO_CAT_PATH_CAP];
    snprintf(relative, sizeof(relative), "resources/%s/%s.png", group, name);
    bongo_cat_path_join(path, BONGO_CAT_PATH_CAP, value->directory, relative);
    if (bongo_cat_path_is_file(path)) return true;
    if (name[0] == 'F' && name[1] >= '0' && name[1] <= '9') {
        snprintf(relative, sizeof(relative), "resources/%s/Fn.png", group);
        bongo_cat_path_join(path, BONGO_CAT_PATH_CAP, value->directory, relative);
        return bongo_cat_path_is_file(path);
    }
    return false;
}

int bongo_cat_overlay_key(BongoCatOverlay *value, const char *name, bool pressed) {
    if (!value || !name) return -1;
    bool right;
    char path[BONGO_CAT_PATH_CAP];
    if (key_path(value, "right-keys", name, path)) right = true;
    else if (key_path(value, "left-keys", name, path)) right = false;
    else return -1;
    char *active_name = right ? value->right_name : value->left_name;
    GLuint *active = right ? &value->right : &value->left;
    char *active_path = right ? value->right_path : value->left_path;
    if (pressed) {
        snprintf(active_name, BONGO_CAT_ID_CAP, "%s", name);
#ifdef BONGO_CAT_HAS_CUBISM
        *active = bongo_cat_overlay_cached_texture(value, path);
#else
        snprintf(active_path, BONGO_CAT_PATH_CAP, "%s", path);
        *active = 1;
#endif
    } else if (strcmp(active_name, name) == 0) {
        active_name[0] = '\0';
        *active = 0;
        active_path[0] = '\0';
    }
#ifndef BONGO_CAT_HAS_CUBISM
    value->composite_dirty = true;
#endif
    return right ? 1 : 0;
}

bool bongo_cat_overlay_hand_active(const BongoCatOverlay *value, bool right) {
    return value && (right ? value->right : value->left) != 0;
}

bool bongo_cat_overlay_effect(BongoCatOverlay *value, const char *path) {
    if (!value) return false;
    value->effect = 0;
    value->effect_path[0] = '\0';
    if (!path || !*path) return true;
    if (!bongo_cat_path_is_file(path)) return false;
#ifdef BONGO_CAT_HAS_CUBISM
    value->effect = bongo_cat_overlay_cached_texture(value, path);
#else
    value->effect = 1;
#endif
    snprintf(value->effect_path, sizeof(value->effect_path), "%s", path);
    return value->effect != 0;
}
