#include "cubism_model.hpp"
#include "model_frame_policy.h"

#include <algorithm>
#include <cmath>

namespace bongo_cat {

void NativeModel::update_viewport() {
    BongoCatFrameViewport v = bongo_cat_frame_viewport(frame_, required_frame_,
        std::max(1, width_), std::max(1, height_), vertical_flip_);
    viewport_x_ = v.x;
    viewport_y_ = v.y;
    viewport_width_ = v.width;
    viewport_height_ = v.height;
    frame_fit_scale_ = v.scale;
}

void NativeModel::set_frame(const BongoCatLive2DFrame &frame) {
    if (!bongo_cat_frame_valid(frame)) return;
    frame_ = frame;
    update_viewport();
}

bool NativeModel::frame(BongoCatLive2DFrame *frame) const {
    if (!_model || !frame) return false;
    *frame = frame_;
    return true;
}

bool NativeModel::viewport(int *x, int *y, int *width, int *height) const {
    if (!_model || !x || !y || !width || !height) return false;
    *x = viewport_x_;
    *y = viewport_y_;
    *width = viewport_width_;
    *height = viewport_height_;
    return true;
}

void NativeModel::apply_viewport_projection(
    Csm::CubismMatrix44 &projection) const {
    if (width_ <= 0 || height_ <= 0 || viewport_width_ <= 0 ||
        viewport_height_ <= 0) return;
    float scale_x = (float)viewport_width_ / (float)width_;
    float scale_y = (float)viewport_height_ / (float)height_;
    float translate_x = (2.0f * viewport_x_ + viewport_width_) /
        (float)width_ - 1.0f;
    float translate_y = (2.0f * viewport_y_ + viewport_height_) /
        (float)height_ - 1.0f;
    float *matrix = projection.GetArray();
    matrix[0] *= scale_x;
    matrix[4] *= scale_x;
    matrix[12] = matrix[12] * scale_x + translate_x;
    matrix[1] *= scale_y;
    matrix[5] *= scale_y;
    matrix[13] = matrix[13] * scale_y + translate_y;
}

void NativeModel::record_visible_state(Csm::CubismMatrix44 &projection) const {
    visual_state_.drawable_count = _model->GetDrawableCount();
    for (int i = 0; i < _model->GetDrawableCount(); ++i) {
        if (_model->GetDrawableDynamicFlagIsVisible(i) &&
            _model->GetDrawableOpacity(i) > 0.001f)
            ++visual_state_.drawable_visible;
        if (_model->GetDrawableDynamicFlagVertexPositionsDidChange(i))
            ++visual_state_.drawable_vertex_changed;
    }
    visual_state_.offscreen_count = _model->GetOffscreenCount();
    for (int i = 0; i < _model->GetOffscreenCount(); ++i)
        if (_model->GetOffscreenOpacity(i) > 0.001f)
            ++visual_state_.offscreen_positive;
    visual_state_.part_count = _model->GetPartCount();
    for (int i = 0; i < _model->GetPartCount(); ++i)
        if (_model->GetPartOpacity(i) > 0.001f)
            ++visual_state_.part_positive;
    if (_model->GetModelOpacity() <= 0.001f) return;
    ModelBounds bounds = capture_visible_bounds();
    if (!bounds.valid) return;
    float x0 = projection.TransformX(bounds.min_x);
    float x1 = projection.TransformX(bounds.max_x);
    float y0 = projection.TransformY(bounds.min_y);
    float y1 = projection.TransformY(bounds.max_y);
    visual_state_.visible_min_x = std::min(x0, x1);
    visual_state_.visible_max_x = std::max(x0, x1);
    visual_state_.visible_min_y = std::min(y0, y1);
    visual_state_.visible_max_y = std::max(y0, y1);
    visual_state_.visible = true;
}

bool NativeModel::visual_state(BongoCatLive2DVisualState *state) const {
    if (!state || !visual_state_ready_) return false;
    if (!visual_state_cached_) {
        // Bounds are used by pointer anchoring and visual audits, not drawing.
        // Avoid traversing every triangle on every animated frame.
        bool mver_projection = visual_state_.mver_projection;
        float fit_scale = visual_state_.fit_scale;
        visual_state_ = BongoCatLive2DVisualState{};
        visual_state_.fit_scale = fit_scale;
        visual_state_.fitted = fit_scale < 0.9999f;
        visual_state_.mver_projection = mver_projection;
        record_visible_state(visual_projection_);
        visual_state_cached_ = true;
    }
    *state = visual_state_;
    return true;
}

} // namespace bongo_cat
