#ifndef BONGO_CAT_MOUSE_DIAGNOSTICS_H
#define BONGO_CAT_MOUSE_DIAGNOSTICS_H

#include "bongo_cat/app.h"

#include <SDL3/SDL.h>

void bongo_cat_mouse_audit(BongoCatApp *app, double x, double y);
void bongo_cat_mouse_log_diagnostics(BongoCatApp *app, uint64_t now,
    bool received, double target_x, double target_y,
    float global_x, float global_y, SDL_MouseButtonFlags buttons,
    bool cursor_locked, bool relative_requested);
void bongo_cat_mouse_log_mode_change(const BongoCatApp *app,
    bool cursor_locked, bool profile_relative, bool relative_requested,
    bool map_requested, bool map_ok, double target_x, double target_y,
    double model_x, double model_y, bool model_moved);

#endif
