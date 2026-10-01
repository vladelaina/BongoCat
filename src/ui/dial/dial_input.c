#include "dial_internal.h"

static void pointer(Dial *d, float px, float py, bool click, int *root, int *child) {
    float scale = d->scale*d->opening;
    float x = (px-(float)d->width/2)/scale, y = (py-(float)d->height/2)/scale;
    float radius = hypotf(x,y);
    if (d->child_focus && radius < 190) { d->child_focus = false; d->dirty = true; }
    dial_hit(d,x,y,root,child);
    if (click && *child < 0 && *root >= 0) {
        float angle = atan2f(y,x)-(-DIAL_PI/2+(float)*root*2*DIAL_PI/(float)d->count);
        if (radius < 73 || radius > 198 ||
            fabsf(atan2f(sinf(angle),cosf(angle))) > DIAL_PI/(float)d->count) *root = -1;
    }
}

static void activate(Dial *d) {
    if (d->active < 0) return;
    char text[32];
    DialItem item = d->child < 0 ? d->items[d->active] : dial_child_item(d,d->child,text,sizeof(text));
    if (item.children) { dial_select(d,d->active,0); return; }
    if (item.command != BONGO_CAT_MENU_NONE) { d->result = item.command; d->done = true; }
}

static void page(Dial *d, int direction) {
    if (d->active < 0 || d->items[d->active].children <= DIAL_PAGE) return;
    int pages = ((int)d->items[d->active].children+DIAL_PAGE-1)/DIAL_PAGE;
    d->page = (d->page+direction+pages)%pages;
    d->child = -1;
    if (d->preview != BONGO_CAT_MENU_NONE) dial_preview(d,BONGO_CAT_MENU_NONE);
    d->preview = BONGO_CAT_MENU_NONE;
    d->changed_at = SDL_GetTicks();
    dial_child_paths(d); d->dirty = true;
}

static void key(Dial *d, const SDL_KeyboardEvent *event) {
    SDL_Keycode value = event->key;
    if (value == SDLK_ESCAPE) {
        if (d->active >= 0 && dial_child_count(d)) dial_select(d,-1,-1);
        else d->done = true;
    } else if (value == SDLK_RETURN || value == SDLK_SPACE) activate(d);
    else if (value == SDLK_PAGEUP || value == SDLK_PAGEDOWN) page(d,value == SDLK_PAGEDOWN ? 1 : -1);
    else if (value == SDLK_LEFT || value == SDLK_UP || value == SDLK_RIGHT ||
        value == SDLK_DOWN || value == SDLK_TAB) {
        int direction = value == SDLK_LEFT || value == SDLK_UP ||
            (value == SDLK_TAB && (event->mod & SDL_KMOD_SHIFT)) ? -1 : 1;
        int count = dial_child_count(d);
        if (d->child >= 0 && count) dial_select(d,d->active,(d->child+direction+count)%count);
        else dial_select(d,d->active < 0 ? 0 : (d->active+direction+d->count)%d->count,-1);
    } else if (value == SDLK_BACKSPACE) {
        d->child_focus = false; d->dirty = true; dial_select(d,d->active,-1);
    }
}

void dial_event(Dial *d, const SDL_Event *e) {
    int root, child;
    switch (e->type) {
    case SDL_EVENT_MOUSE_MOTION:
        pointer(d,e->motion.x,e->motion.y,false,&root,&child);
        dial_select(d,root,child); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button != SDL_BUTTON_LEFT) { d->done = true; break; }
        pointer(d,e->button.x,e->button.y,true,&root,&child);
        if (root < 0) { d->done = true; break; }
        dial_select(d,root,child);
        d->pressed = child >= 0 ? DIAL_ROOTS+child : root; d->dirty = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e->button.button != SDL_BUTTON_LEFT) break;
        int pressed = d->pressed; d->pressed = -1; d->dirty = true;
        pointer(d,e->button.x,e->button.y,true,&root,&child);
        if (root >= 0 && pressed == (child >= 0 ? DIAL_ROOTS+child : root)) {
            dial_select(d,root,child); activate(d);
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        float delta = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
        if (delta != 0) page(d,delta < 0 ? 1 : -1);
        break;
    }
    case SDL_EVENT_KEY_DOWN: key(d,&e->key); break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: dial_select(d,-1,-1); break;
    case SDL_EVENT_WINDOW_SHOWN: d->shown = true; d->dirty = true; break;
    case SDL_EVENT_WINDOW_HIDDEN:
        if (d->shown && (SDL_GetWindowFlags(d->window) & SDL_WINDOW_HIDDEN)) d->done = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        /* Creation/show may queue a transient loss before focus settles. */
        if (d->shown && SDL_GetKeyboardFocus() != d->window) d->done = true;
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: d->done = true; break;
    case SDL_EVENT_WINDOW_EXPOSED: d->dirty = true; break;
    case SDL_EVENT_WINDOW_RESIZED: case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        if (!SDL_GetWindowSize(d->window,&d->width,&d->height) ||
            !SDL_GetWindowSizeInPixels(d->window,&d->pixel_width,&d->pixel_height) ||
            d->width < 1 || d->height < 1) { d->done = true; break; }
        d->scale = (float)SDL_min(d->width,d->height)/608.0f;
        d->raster_scale = (float)d->pixel_width/(float)d->width;
        /* SDL also sends DISPLAY_SCALE_CHANGED during window creation.
           Keep the menu open; existing textures remain valid and new covers
           use the updated density. Fonts are rebaked on the next opening. */
        d->dirty = true;
        break;
    }
}
