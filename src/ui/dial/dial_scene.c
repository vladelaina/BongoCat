#include "dial_internal.h"
#include <stdio.h>

static float reveal(uint64_t now, uint64_t started, int index) {
    float elapsed = (float)(now - started) - index * DIAL_REVEAL_DELAY_MS;
    float t = fmaxf(0, fminf(1, elapsed / DIAL_REVEAL_DURATION_MS));
    return 1 - powf(1 - t, 3);
}

static uint32_t alpha(uint32_t color, float opacity) {
    return (color&0xffffff) | ((uint32_t)((float)(color>>24)*opacity)<<24);
}
static uint32_t mix(uint32_t a, uint32_t b, float t) {
    uint32_t color = 0;
    for (int shift = 0; shift <= 24; shift += 8) {
        float av = (float)((a>>shift)&255), bv = (float)((b>>shift)&255);
        color |= (uint32_t)(av+(bv-av)*t)<<shift;
    }
    return color;
}
static uint32_t gradient(const DialPath *p, DialPoint v, const uint32_t *colors, float opacity) {
    float t = .5f*((v.x-p->min_x)/(p->max_x-p->min_x)+(v.y-p->min_y)/(p->max_y-p->min_y));
    t = fmaxf(0,fminf(1,t));
    return alpha(t < .5f ? mix(colors[0],colors[1],t*2) : mix(colors[1],colors[2],(t-.5f)*2),opacity);
}
static void surface(Dial *d, const DialPath *p, int tint, float opacity) {
    if (!p->count) return;
    uint32_t colors[3];
    if (tint == 1) { colors[0]=0xff6ec2ff; colors[1]=0xff54aeff; colors[2]=0xff1e82f0; }
    else if (tint == 2) { colors[0]=0xffff97be; colors[1]=0xfff77daa; colors[2]=0xffe34882; }
    else if (tint == 3) { colors[0]=0xffff6960; colors[1]=0xffff453a; colors[2]=0xffd10014; }
    else if (d->dark) { colors[0]=0xff010409; colors[1]=0xff010409; colors[2]=0xff010409; }
    else { colors[0]=0xffffffff; colors[1]=0xfff6f9ff; colors[2]=0xffeef3fc; }
    DialPoint closed[DIAL_PATH_POINTS+1];
    for (int i = 0; i < p->count; ++i) closed[i]=p->points[i];
    closed[p->count]=p->points[0];
    uint32_t glow = tint ? (colors[1]&0xffffff)|0x06000000 : 0x03000000;
    for (int i = 6; i > 0; --i)
        dial_stroke(d,closed,p->count+1,(float)i*5,alpha(glow,opacity),false);
    /* Shared polygon vertices only need their gradient calculated once. */
    uint32_t vertex_colors[DIAL_PATH_POINTS];
    for (int i = 0; i < p->count; ++i)
        vertex_colors[i] = gradient(p,p->points[i],colors,opacity);
    for (int i = 0; i < p->triangle_count; i += 3) {
        int a=p->triangles[i], b=p->triangles[i+1], c=p->triangles[i+2];
        dial_triangle(d,p->points[a],p->points[b],p->points[c],
            vertex_colors[a],vertex_colors[b],vertex_colors[c]);
    }
    dial_stroke(d,closed,p->count+1,1.1f,alpha(tint || !d->dark ? 0xfaffffff : 0x38ffffff,opacity),true);
    dial_stroke(d,closed+p->rim_start,p->count+1-p->rim_start,1.4f,
        alpha(d->dark ? 0xd3ffffff : 0xebffffff,opacity),true);
}
static void text(Dial *d, const char *s, float x, float y, float width, bool title, uint32_t color) {
    dial_text(d,s,x,y,width,title ? 19.0f : 14.0f,color);
}
static int item_icon(DialItem item) {
    return item.checked && item.icon == 3 ? 13 : item.icon;
}

static void root(Dial *d, int index, uint64_t now) {
    DialItem item = d->items[index];
    float angle = -DIAL_PI / 2 + index * 2 * DIAL_PI / d->count;
    float x = 132 * cosf(angle), y = 132 * sinf(angle);
    float ease = reveal(now, d->opened_at, index);
    /* Keep the first sector hittable on transparent native windows. */
    if (index == 0) ease = fmaxf(.15f, ease);
    float lift = d->lift[index], zoom = .72f + .28f * ease + .055f * lift;
    float pull = -26 * (1 - ease) + 7.5f * lift;
    if (d->pressed == index) { zoom *= .96f; pull *= .4f; }
    dial_transform(d, zoom, x * (1 - zoom) + cosf(angle) * pull,
        y * (1 - zoom) + sinf(angle) * pull);
    bool active = index == d->active;
    float opacity = ease;
    int tint = active ? (index >= 10 ? 3 : 1) : 0;
    surface(d, &d->paint.roots[index], tint, opacity);
    bool top_checked = item.command == BONGO_CAT_MENU_ALWAYS_ON_TOP && item.checked;
    uint32_t color = active ? 0xffffffff :
        (item.checked ? (top_checked ? 0xff60a5fa : 0xfff77daa) : item.color);
    if (index >= 6 && index <= 8 && !item.children) opacity *= .4f;
    dial_icon(d, item_icon(item), x, y, 28 * (1 + .15f * lift), alpha(color, opacity));
    if (item.checked && item.command != BONGO_CAT_MENU_ALWAYS_ON_TOP) {
        for (int i = 4; i > 0; --i)
            dial_dot(d, x + 17, y - 17, 3.2f + i * 1.5f,
                alpha(top_checked ? 0x0a60a5fa : 0x0af77daa, ease));
        dial_dot(d, x + 17, y - 17, 3.2f,
            alpha(top_checked ? 0xff60a5fa : 0xfff77daa, ease));
    }
}

static void center_background(Dial *d) {
    enum { segments = DIAL_CENTER_SEGMENTS, rings = DIAL_CENTER_RINGS };
    const float radius = 80.0f;
    uint32_t color = d->dark ? 0x00010409 : 0x00f6f9ff;
    DialPaint *p = &d->paint;
    /* Cache the original mesh per menu; transforms and opening alpha stay live. */
    if (!p->center_ready) {
        DialPoint directions[segments];
        for (int i = 0; i < segments; ++i) {
            float angle = (float)i * 2 * DIAL_PI / segments;
            directions[i] = (DialPoint){cosf(angle), sinf(angle)};
        }
        for (int ring = 0; ring <= rings; ++ring) {
            float t = (float)ring / rings;
            p->center_alpha[ring] = (uint8_t)(alpha(0xe6000000,
                1 - t * t * (3 - 2 * t)) >> 24);
            for (int i = 0; i < segments; ++i)
                p->center_points[ring][i] = (DialPoint){
                    radius * t * directions[i].x, radius * t * directions[i].y};
        }
        p->center_ready = true;
    }
    /* Adjacent rings share edges; smoothstep fades to a fully clear rim. */
    for (int ring = 0; ring < rings; ++ring) {
        uint32_t ci = color | ((uint32_t)p->center_alpha[ring] << 24);
        uint32_t co = color | ((uint32_t)p->center_alpha[ring+1] << 24);
        for (int i = 0; i < segments; ++i) {
            int next = (i + 1) % segments;
            DialPoint a = p->center_points[ring][i];
            DialPoint b = p->center_points[ring+1][i];
            DialPoint c = p->center_points[ring+1][next];
            DialPoint e = p->center_points[ring][next];
            dial_triangle(d, a, b, c, ci, co, co);
            if (ring > 0) dial_triangle(d, a, c, e, ci, co, ci);
        }
    }
}

static void center(Dial *d) {
    if (d->active < 0) return;
    dial_transform(d, 1, 0, 0);
    center_background(d);
    DialItem item = d->items[d->active];
    char buffer[32];
    bool child_hovered = d->child >= 0;
    uint32_t color = child_hovered ? 0xfff77daa : 0xff52a9f8;
    const char *label = child_hovered ?
        dial_child_item(d, d->child, buffer, sizeof(buffer)).label : item.label;
    if (!child_hovered)
        dial_icon(d, item_icon(item), 0, -5, 38, item.checked ? 0xfff77daa : item.color);
    text(d, label, 0, child_hovered ? 0.0f : 30.0f, 140, true, color);
    if (item.children > DIAL_PAGE) {
        snprintf(buffer, sizeof(buffer), "%d / %d", d->page + 1,
            ((int)item.children + DIAL_PAGE - 1) / DIAL_PAGE);
        text(d, buffer, 0, 58, 80, false, color);
    }
}

static void children(Dial *d, uint64_t now) {
    for (int i = 0; i < dial_child_count(d); ++i) {
        /* Child indices increase with screen angle, so this reveals clockwise. */
        float ease = reveal(now, d->changed_at, i);
        float angle = dial_child_angle(d, i), x = 230 * cosf(angle), y = 230 * sinf(angle);
        bool hover = i == d->child;
        float zoom = .72f + .28f * ease + (hover ? .06f : 0);
        float pull = -26 * (1 - ease) + (hover ? 6 : 0);
        dial_transform(d, zoom, x * (1 - zoom) + cosf(angle) * pull,
            y * (1 - zoom) + sinf(angle) * pull);
        char buffer[32];
        DialItem item = dial_child_item(d, i, buffer, sizeof(buffer));
        surface(d, &d->paint.children[i],
            item.checked ? 2 : (hover ? 1 : 0), ease);
        uint32_t color = hover || item.checked || d->dark ? 0xffffffff : 0xff181c28;
        if (!dial_cover_draw(d, i, x, y, ease))
            text(d, item.label, x, y, dial_child_step(d) > .4f ? 92.0f : 62.0f,
                false, alpha(color, ease));
    }
}

void dial_scene(Dial *d) {
    uint64_t now = SDL_GetTicks();
    for (int i = 0; i < d->count; ++i)
        if (!d->child_focus || i == d->active) root(d,i,now);
    center(d);
    children(d,now);
}
