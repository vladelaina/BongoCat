#include "dial_internal.h"
#include <stdio.h>

void dial_items(Dial *d) {
    const BongoCatMenuLabels *l = d->labels;
    const DialItem items[] = {
        {l->preferences, BONGO_CAT_MENU_PREFERENCES, 0xff54aeff, 0, false, 0},
        {l->hide, BONGO_CAT_MENU_HIDE, 0xff60a5fa, 1, false, 0},
        {l->pass_through, BONGO_CAT_MENU_PASS_THROUGH, 0xff38bdf8, 2,
            l->pass_through_checked, 0},
        {l->always_on_top, BONGO_CAT_MENU_ALWAYS_ON_TOP, 0xfff77daa, 3,
            l->always_on_top_checked, 0},
        {l->window_size, BONGO_CAT_MENU_NONE, 0xff34d399, 4, false, 16},
        {l->opacity, BONGO_CAT_MENU_NONE, 0xff818cf8, 5, false, 10},
        {l->motion, BONGO_CAT_MENU_NONE, 0xfffbbf24, 6, false, l->motion_count},
        {l->expression, BONGO_CAT_MENU_NONE, 0xfff472b6, 7, false, l->expression_count},
        {l->audio, BONGO_CAT_MENU_NONE, 0xff4ade80, 8, false, l->audio_count},
        {l->model, BONGO_CAT_MENU_NONE, 0xffa78bfa, 9, false, l->model_count + 1},
        {l->exit, BONGO_CAT_MENU_EXIT, 0xffff453a, 10, false, 0},
        {l->remove_pet, BONGO_CAT_MENU_REMOVE_PET, 0xffff453a, 11, false, 0}
    };
    d->count = l->remove_pet_visible ? 12 : 11;
    for (int i = 0; i < d->count; ++i) d->items[i] = items[i];
}

int dial_child_count(const Dial *d) {
    if (d->active < 0) return 0;
    int remaining = (int)d->items[d->active].children - d->page * DIAL_PAGE;
    return remaining < 0 ? 0 : (remaining > DIAL_PAGE ? DIAL_PAGE : remaining);
}

float dial_child_step(const Dial *d) {
    /* Preserve HTML spacing; large real catalogs use pages to avoid overlap. */
    return d->active == 4 || d->active == 5 || dial_child_count(d) > 11 ? .32f : .50f;
}

float dial_child_angle(const Dial *d, int index) {
    return -DIAL_PI / 2 + d->active * 2 * DIAL_PI / d->count +
        (index - (dial_child_count(d) - 1) / 2.0f) * dial_child_step(d);
}

DialItem dial_child_item(const Dial *d, int child, char *text, size_t capacity) {
    DialItem item = {0};
    if (child < 0 || child >= dial_child_count(d)) return item;
    const BongoCatMenuLabels *l = d->labels;
    size_t i = (size_t)(d->page * DIAL_PAGE + child);
    item = d->items[d->active];
    item.children = 0;
    switch (d->active) {
    case 4:
        snprintf(text, capacity, "%d%%", 50 + (int)i * 10);
        item.label = text;
        item.command = (BongoCatMenuAction)(BONGO_CAT_MENU_SCALE_50 + i);
        item.checked = fabsf(l->scale_percent - (50 + (int)i * 10)) < .5f;
        break;
    case 5:
        snprintf(text, capacity, "%d%%", 10 + (int)i * 10);
        item.label = text;
        item.command = (BongoCatMenuAction)(BONGO_CAT_MENU_OPACITY_10 + i);
        item.checked = fabsf(l->opacity_percent - (10 + (int)i * 10)) < .5f;
        break;
    case 6:
        item.label = l->motion_names[i];
        item.command = (BongoCatMenuAction)(BONGO_CAT_MENU_MOTION_FIRST + i);
        item.checked = l->motion_checked && l->motion_checked[i];
        break;
    case 7:
        item.label = l->expression_names[i];
        item.command = (BongoCatMenuAction)(BONGO_CAT_MENU_EXPRESSION_FIRST + i);
        item.checked = i == l->current_expression;
        break;
    case 8:
        item.label = l->audio_names[i];
        item.command = (BongoCatMenuAction)(BONGO_CAT_MENU_AUDIO_FIRST + i);
        item.checked = l->audio_checked && l->audio_checked[i];
        break;
    case 9:
        item.label = i == l->model_count ? l->add_model : l->model_names[i];
        item.command = i == l->model_count ? BONGO_CAT_MENU_MODEL_ADD :
            (BongoCatMenuAction)(BONGO_CAT_MENU_MODEL_FIRST + i);
        item.checked = i < l->model_count && i == l->current_model;
        break;
    }
    return item;
}

void dial_select(Dial *d, int root, int child) {
    if (d->active == root && d->child == child) return;
    bool changed = d->active != root;
    d->active = root;
    d->child = child;
    if (changed) d->child_focus = false;
    if (child >= 0) d->child_focus = true;
    if (changed) {
        d->page = 0;
        d->changed_at = SDL_GetTicks();
        dial_child_paths(d);
    }
    char text[32];
    BongoCatMenuAction action = child >= 0 ?
        dial_child_item(d, child, text, sizeof(text)).command : BONGO_CAT_MENU_NONE;
    if (action != d->preview) {
        d->preview = action;
        dial_preview(d, action);
    }
    d->dirty = true;
}
