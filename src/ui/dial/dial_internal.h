#ifndef BONGO_CAT_DIAL_INTERNAL_H
#define BONGO_CAT_DIAL_INTERNAL_H
#include "bongo_cat/platform.h"
#include "bongo_cat/gl_api.h"
#include "nuklear_config.h"
#include <SDL3/SDL.h>
#include <math.h>
#define DIAL_PI 3.14159265358979323846f
#define DIAL_PAGE 16
#define DIAL_ROOTS 12
#define DIAL_REVEAL_DELAY_MS 22
#define DIAL_REVEAL_DURATION_MS 360
#define DIAL_PATH_POINTS 160
#define DIAL_CENTER_SEGMENTS 64
#define DIAL_CENTER_RINGS 16
typedef struct DialPoint { float x, y; } DialPoint;
typedef struct DialPath {
    DialPoint points[DIAL_PATH_POINTS];
    unsigned short triangles[DIAL_PATH_POINTS * 3];
    int count, triangle_count, rim_start;
    float min_x, min_y, max_x, max_y;
} DialPath;
typedef struct DialItem {
    const char *label;
    BongoCatMenuAction command;
    uint32_t color;
    int icon;
    bool checked;
    size_t children;
} DialItem;
typedef struct DialVertex { float x, y, u, v; uint8_t r, g, b, a; } DialVertex;
typedef struct DialBatch { GLuint texture; int first, count; } DialBatch;
typedef struct DialCover { GLuint texture; float width, height; bool attempted; } DialCover;
typedef struct DialPaint {
    BongoCatGL gl;
    GLuint program, vao, vbo, white, font_texture;
    GLint projection, sampler;
    DialVertex *vertices;
    size_t count, capacity;
    DialBatch batches[512];
    int batch_count;
    struct nk_font_atlas atlas;
    struct nk_font *font;
    nk_rune *ranges[4];
    DialPath roots[DIAL_ROOTS], children[DIAL_PAGE];
    int child_path_count, child_path_active, child_path_roots;
    float child_path_step;
    float zoom, offset_x, offset_y, alpha;
    DialPoint center_points[DIAL_CENTER_RINGS + 1][DIAL_CENTER_SEGMENTS];
    uint8_t center_alpha[DIAL_CENTER_RINGS + 1];
    bool center_ready;
    bool failed;
} DialPaint;
typedef struct Dial {
    SDL_Window *window, *owner;
    SDL_GLContext context, previous_context;
    SDL_Window *previous_window;
    SDL_WindowID window_id;
    const BongoCatMenuLabels *labels;
    DialItem items[DIAL_ROOTS];
    int count, active, child, page, pressed;
    bool done, dark, dirty, child_focus, shown, popup;
    uint64_t input_after_ns;
    BongoCatMenuAction result, preview;
    int width, height, pixel_width, pixel_height;
    float scale, opening, lift[DIAL_ROOTS], raster_scale;
    uint64_t opened_at, changed_at, animation_at;
    SDL_Event events[128];
    int event_count;
    DialPaint paint;
    DialCover covers[BONGO_CAT_MODEL_CAP];
} Dial;
void dial_items(Dial *d);
int dial_child_count(const Dial *d);
float dial_child_angle(const Dial *d, int index);
float dial_child_step(const Dial *d);
DialItem dial_child_item(const Dial *d, int index, char *text, size_t capacity);
void dial_select(Dial *d, int root, int child);
void dial_hit(Dial *d, float x, float y, int *root, int *child);
void dial_child_paths(Dial *d);
void dial_sector(DialPath *path, float inner, float outer, float start, float end);
void dial_transform(Dial *d, float zoom, float x, float y);
bool dial_paint_init(Dial *d);
void dial_paint_free(Dial *d);
bool dial_paint_frame(Dial *d);
void dial_scene(Dial *d);
void dial_triangle(Dial *d, DialPoint a, DialPoint b, DialPoint c,
    uint32_t ca, uint32_t cb, uint32_t cc);
void dial_quad(Dial *d, GLuint texture, float x, float y, float w, float h,
    float u0, float v0, float u1, float v1, uint32_t color);
void dial_stroke(Dial *d, const DialPoint *points, int count, float width,
    uint32_t color, bool smooth);
void dial_dot(Dial *d, float x, float y, float radius, uint32_t color);
void dial_icon(Dial *d, int icon, float x, float y, float size, uint32_t color);
bool dial_fonts_init(Dial *d);
void dial_text(Dial *d, const char *text, float x, float y, float width,
    float size, uint32_t color);
void dial_covers_tick(Dial *d);
bool dial_cover_draw(Dial *d, int child, float x, float y, float opacity);
void dial_event(Dial *d, const SDL_Event *event);
void dial_preview(Dial *d, BongoCatMenuAction action);
#endif
