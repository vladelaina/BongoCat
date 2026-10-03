#include "cubism_model.hpp"

#include <SDL3/SDL_log.h>

namespace bongo_cat {

void NativeModel::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    bool changed = width != width_ || height != height_;
    width_ = width;
    height_ = height;
    update_viewport();
    if (changed) schedule_texture_refresh();
    if (!_model || (width == renderer_width_ && height == renderer_height_)) return;
    if (!_model->IsBlendModeEnabled()) {
        renderer_width_ = width_;
        renderer_height_ = height_;
        return;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO,
        "Live2D render target resized in place: %dx%d -> %dx%d",
        renderer_width_, renderer_height_, width_, height_);
    SetRenderTargetSize((Csm::csmUint32)width_, (Csm::csmUint32)height_);
    renderer_width_ = width_;
    renderer_height_ = height_;
}

void NativeModel::reshape(int width, int height) {
    if (width > 0 && height > 0) {
        bool changed = width != width_ || height != height_;
        width_ = width;
        height_ = height;
        update_viewport();
        if (changed) schedule_texture_refresh();
    }
}

void NativeModel::build_projection(Csm::CubismMatrix44 &projection,
    int width, int height) {
    if (!_model || width <= 0 || height <= 0) return;
    Csm::CubismModelMatrix model_matrix(*_modelMatrix);
    if (render_options_.mver_projection) {
        float aspect = (float)render_options_.reference_width /
            (float)render_options_.reference_height;
        projection.Scale((mirror_ ? -1.0f : 1.0f) *
            render_options_.projection_scale,
            render_options_.projection_scale * aspect);
        projection.Translate(render_options_.offset_x, render_options_.offset_y);
    } else if (_model->GetCanvasWidth() > 1.0f && width < height) {
        model_matrix.SetWidth(2.0f);
        projection.Scale(mirror_ ? -1.0f : 1.0f,
            (float)width / (float)height);
    } else {
        projection.Scale((mirror_ ? -1.0f : 1.0f) *
            (float)height / (float)width, 1.0f);
    }
    projection.MultiplyByMatrix(&model_matrix);
    if (render_options_.mver_projection) {
        // Mver 0.1.6's Core reports canvas units at twice the modern scale.
        // Preserve authored Layout translation while matching its model scale.
        float *matrix = projection.GetArray();
        constexpr float mver_core_canvas_scale = 0.5f;
        matrix[0] *= mver_core_canvas_scale;
        matrix[1] *= mver_core_canvas_scale;
        matrix[4] *= mver_core_canvas_scale;
        matrix[5] *= mver_core_canvas_scale;
    }
}

void NativeModel::set_mirror(bool mirror) { mirror_ = mirror; }

void NativeModel::set_render_options(const BongoCatLive2DRenderOptions &options) {
    render_options_ = options;
}

} // namespace bongo_cat
