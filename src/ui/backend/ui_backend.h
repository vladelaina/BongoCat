#ifndef BONGO_CAT_UI_BACKEND_H
#define BONGO_CAT_UI_BACKEND_H

#include "bongo_cat/gl_api.h"
#include <SDL3/SDL.h>
#include "nuklear_config.h"
#include <stdint.h>

typedef enum BongoCatUICursor {
    BONGO_CAT_UI_CURSOR_DEFAULT,
    BONGO_CAT_UI_CURSOR_POINTER,
    BONGO_CAT_UI_CURSOR_TEXT,
    BONGO_CAT_UI_CURSOR_RESIZE_EW
} BongoCatUICursor;

typedef struct BongoCatUIBackend {
    SDL_Window *window;
    float layout_scale;
    float raster_scale;
    BongoCatGL gl;
    struct nk_context context;
    struct nk_font_atlas atlas;
    struct nk_buffer commands;
    struct nk_draw_null_texture null_texture;
    GLuint program;
    GLint texture_location;
    GLint projection_location;
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    GLuint font_texture;
    GLuint resize_cache_texture;
    int resize_cache_width;
    int resize_cache_height;
    unsigned resize_cache_presentations;
    unsigned resize_cache_failures;
    int font_atlas_width;
    int font_atlas_height;
    const struct nk_user_font *caption_font;
    const struct nk_user_font *body_font;
    const struct nk_user_font *label_font;
    const struct nk_user_font *heading_font;
    const struct nk_user_font *hero_font;
    nk_rune *latin_glyph_ranges;
    nk_rune *cjk_glyph_ranges;
    nk_rune *korean_glyph_ranges;
    void *vertices;
    void *elements;
    size_t vertex_capacity;
    size_t element_capacity;
    size_t last_vertex_bytes;
    size_t last_element_bytes;
    uint64_t paint_frame_marker;
    unsigned last_draw_commands;
    unsigned last_draw_elements;
    int last_convert_result;
    GLenum last_gl_error;
    unsigned nonzero_alpha_vertices;
    unsigned max_alpha;
    uint64_t last_left_click_ns;
    int last_left_click_x;
    int last_left_click_y;
    bool custom_font_loaded;
    bool font_probe_loaded;
    bool font_path_found;
    bool font_file_loaded;
    bool dark_theme;
    bool live_resize_fast;
    bool frame_building;
    bool double_click_down;
    SDL_Cursor *default_cursor;
    SDL_Cursor *pointer_cursor;
    SDL_Cursor *text_cursor;
    SDL_Cursor *resize_ew_cursor;
    BongoCatUICursor requested_cursor;
} BongoCatUIBackend;

bool bongo_cat_ui_init(BongoCatUIBackend *ui, SDL_Window *window,
    const char *body_font_path, const char *body_fallback_path,
    const char *body_korean_fallback_path, const char *heading_font_path,
    const char *heading_fallback_path,
    const char *heading_korean_fallback_path,
    const nk_rune *glyph_ranges, float layout_scale, float raster_scale,
    BongoCatError *error);
void bongo_cat_ui_destroy(BongoCatUIBackend *ui);
void bongo_cat_ui_abandon(BongoCatUIBackend *ui);
void bongo_cat_ui_input_begin(BongoCatUIBackend *ui);
void bongo_cat_ui_input_end(BongoCatUIBackend *ui);
void bongo_cat_ui_input_reset(BongoCatUIBackend *ui);
bool bongo_cat_ui_event(BongoCatUIBackend *ui, const SDL_Event *event);
bool bongo_cat_ui_render(BongoCatUIBackend *ui);
void bongo_cat_ui_trim_idle(BongoCatUIBackend *ui);
bool bongo_cat_ui_resize_cache_capture(BongoCatUIBackend *ui);
bool bongo_cat_ui_resize_cache_present(BongoCatUIBackend *ui);
void bongo_cat_ui_resize_cache_destroy(BongoCatUIBackend *ui);
bool bongo_cat_ui_frame_valid(const BongoCatUIBackend *ui);
BongoCatUIBackend *bongo_cat_ui_backend_for_context(
    const struct nk_context *context);
float bongo_cat_ui_display_layout_scale(SDL_DisplayID display);
void bongo_cat_ui_query_window_scale(SDL_Window *window,
    float *layout_scale, float *raster_scale);
void bongo_cat_ui_logical_size(const BongoCatUIBackend *ui,
    float *width, float *height);
const struct nk_user_font *bongo_cat_ui_caption_font(
    const struct nk_context *context);
const struct nk_user_font *bongo_cat_ui_body_font(
    const struct nk_context *context);
const struct nk_user_font *bongo_cat_ui_label_font(
    const struct nk_context *context);
const struct nk_user_font *bongo_cat_ui_heading_font(
    const struct nk_context *context);
const struct nk_user_font *bongo_cat_ui_hero_font(
    const struct nk_context *context);
void bongo_cat_ui_cursor_begin(BongoCatUIBackend *ui);
void bongo_cat_ui_cursor_apply(BongoCatUIBackend *ui);
void bongo_cat_ui_cursor_destroy(BongoCatUIBackend *ui);
void bongo_cat_ui_cursor_reset(struct nk_context *context);
void bongo_cat_ui_cursor_hover_rect(struct nk_context *context,
    struct nk_rect bounds, BongoCatUICursor cursor);
void bongo_cat_ui_cursor_hover_widget(struct nk_context *context,
    BongoCatUICursor cursor);

#endif
