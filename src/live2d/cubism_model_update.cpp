#include "cubism_model.hpp"
#include "cubism_viewer_look.hpp"

#include <Id/CubismIdManager.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Model/CubismModel.hpp>
#include <Motion/CubismExpressionMotionManager.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <algorithm>
#include <cmath>
namespace bongo_cat {

void NativeModel::update_geometry() {
    _model->Update();
    visual_state_cached_ = false;
}

static bool changed(std::vector<float> &snapshot, int count, const float *values) {
    bool result = snapshot.size() != (size_t)count;
    if (result) snapshot.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        float current = values[i];
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
    update_geometry();
    /* These snapshots contain only real Core indices. Read their contiguous
       buffers directly instead of probing Cubism's virtual-ID maps for every
       parameter and part on every frame. Preserve the comparison threshold. */
    bool result = changed(parameter_snapshot_, _model->GetParameterCount(),
        Live2D::Cubism::Core::csmGetParameterValues(_model->GetModel()));
    result = changed(part_snapshot_, _model->GetPartCount(),
        Live2D::Cubism::Core::csmGetPartOpacities(_model->GetModel())) || result;
    if (std::fabs(opacity_snapshot_ - _opacity) > 0.00001f) result = true;
    opacity_snapshot_ = _opacity;
    return result;
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
    update_geometry();
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
