#include "cubism_model.hpp"
#include "cubism_viewer_look.hpp"

#include <Id/CubismIdManager.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Model/CubismModel.hpp>
#include <Motion/CubismExpressionMotionManager.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
namespace bongo_cat {

void NativeModel::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    width_ = width;
    height_ = height;
    update_viewport();
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
        width_ = width;
        height_ = height;
        update_viewport();
    }
}

template<typename Getter>
static bool changed(std::vector<float> &snapshot, int count, Getter value) {
    bool result = snapshot.size() != (size_t)count;
    if (result) snapshot.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        float current = value(i);
        if (result || std::fabs(snapshot[(size_t)i] - current) > 0.00001f) result = true;
        snapshot[(size_t)i] = current;
    }
    return result;
}

bool NativeModel::update(float delta_seconds) {
    if (!_model || delta_seconds <= 0.0f) return false;
    if (delta_seconds > 0.25f) delta_seconds = 0.25f;
    motion_updated_ = suppress_eye_blink_;
    _model->LoadParameters();
    parameter_overrides_applied_ = false;
    if (automatic_idle_ && _motionManager->IsFinished())
        start_idle_motion();
    else if (!_motionManager->IsFinished())
        motion_updated_ = _motionManager->UpdateMotion(_model, delta_seconds);
    expire_motion_runs();
    save_parameters();
    _updateScheduler.OnLateUpdate(_model, delta_seconds);
    expression_frame_pending_ = false;
    expire_expression_fade();
    apply_parameter_overrides();
    _opacity = _model->GetModelOpacity();
    _model->Update();
    bool result = changed(parameter_snapshot_, _model->GetParameterCount(),
        [this](int index) { return _model->GetParameterValue(index); });
    result = changed(part_snapshot_, _model->GetPartCount(),
        [this](int index) { return _model->GetPartOpacity(index); }) || result;
    if (std::fabs(opacity_snapshot_ - _opacity) > 0.00001f) result = true;
    opacity_snapshot_ = _opacity;
    return result;
}

void NativeModel::build_projection(Csm::CubismMatrix44 &projection,
    int width, int height) {
    if (!_model || width <= 0 || height <= 0) return;
    if (render_options_.mver_projection) {
        float aspect = (float)render_options_.reference_width /
            (float)render_options_.reference_height;
        projection.Scale((mirror_ ? -1.0f : 1.0f) *
            render_options_.projection_scale,
            render_options_.projection_scale * aspect);
        projection.Translate(render_options_.offset_x, render_options_.offset_y);
    } else if (_model->GetCanvasWidth() > 1.0f && width < height) {
        _modelMatrix->SetWidth(2.0f);
        projection.Scale(mirror_ ? -1.0f : 1.0f,
            (float)width / (float)height);
    } else {
        projection.Scale((mirror_ ? -1.0f : 1.0f) *
            (float)height / (float)width, 1.0f);
    }
    projection.MultiplyByMatrix(_modelMatrix);
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

void NativeModel::draw() {
    if (!_model || width_ <= 0 || height_ <= 0) return;
#ifdef CSM_TARGET_MAC_GL
    CoreProfileBinding binding(core_buffers_);
#endif
    update_mask_buffers();
    auto *manager = Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance();
    manager->BeginFrameProcess();
    Csm::CubismMatrix44 projection;
    build_projection(projection, viewport_width_, viewport_height_);
    apply_viewport_projection(projection);
    visual_state_ = BongoCatLive2DVisualState{};
    visual_state_.fit_scale = 1.0f;
    visual_state_.mver_projection = render_options_.mver_projection;
    record_visible_state(projection);
    visual_state_ready_ = true;
    auto *renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
    renderer->SetMvpMatrix(&projection);
    renderer->DrawModel();
    manager->EndFrameProcess();
}

void NativeModel::set_mirror(bool mirror) { mirror_ = mirror; }

void NativeModel::set_render_options(const BongoCatLive2DRenderOptions &options) {
    render_options_ = options;
}

void NativeModel::set_dragging(float x, float y, bool angle_z) {
    if (!viewer_look_) return;
    x = std::max(-1.0f, std::min(1.0f, x));
    y = std::max(-1.0f, std::min(1.0f, y));
    viewer_look_->set_target(x, y, angle_z);
}

void NativeModel::prepare_viewer_audit() {
    if (!_model) return;
    automatic_idle_ = false;
    suppress_eye_blink_ = true;
    _motionManager->StopAllMotions();
    clear_motion_runs();
    std::fill(parameter_overrides_.begin(), parameter_overrides_.end(), 0);
    std::fill(parameter_override_values_.begin(),
        parameter_override_values_.end(), 0.0f);
    parameter_overrides_applied_ = false;
    for (int i = 0; i < _model->GetParameterCount(); ++i) {
        _model->SetParameterValue(i, _model->GetParameterDefaultValue(i));
    }
    save_parameters();
}

bool NativeModel::prepare_cover_capture() {
    if (!_model) return false;
    settle_pending_expression_for_cover();
    apply_parameter_overrides();
    /* Persistent motions are already stored in the Cubism baseline. Updating
       drawables does not advance idle motion, physics, breathing, or blinking. */
    _model->Update();
    return true;
}

bool NativeModel::set_parameter(const char *id, float value) {
    if (!_model || !id) return false;
    Csm::CubismIdHandle handle = Csm::CubismFramework::GetIdManager()->GetId(id);
    int index = _model->GetParameterIndex(handle);
    if (index < 0 || index >= _model->GetParameterCount()) return false;
    parameter_override_values_[(size_t)index] = value;
    parameter_overrides_[(size_t)index] = 1;
    if (parameter_overrides_applied_)
        _model->SetParameterValue(index, value);
    else apply_parameter_overrides();
    return true;
}

bool NativeModel::parameter(const char *id, float *minimum, float *maximum, float *value) {
    if (!_model || !id) return false;
    Csm::CubismIdHandle handle = Csm::CubismFramework::GetIdManager()->GetId(id);
    int index = _model->GetParameterIndex(handle);
    if (index < 0 || index >= _model->GetParameterCount()) return false;
    if (minimum) *minimum = _model->GetParameterMinimumValue(index);
    if (maximum) *maximum = _model->GetParameterMaximumValue(index);
    if (value) *value = _model->GetParameterValue(index);
    return true;
}

} // namespace bongo_cat
