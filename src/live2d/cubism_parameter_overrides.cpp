#include "cubism_model.hpp"

#include <Model/CubismModel.hpp>
#include <Motion/ICubismUpdater.hpp>

namespace bongo_cat {

/* External input has to be visible to CubismPhysics, but must remain higher
   priority than physics and pose at frame end.  Running once immediately
   before Physics and once after the scheduler provides both guarantees. */
class ParameterOverrideUpdater final : public Csm::ICubismUpdater {
public:
    explicit ParameterOverrideUpdater(NativeModel &owner)
        : Csm::ICubismUpdater(Csm::CubismUpdateOrder_Physics - 1),
          owner_(owner) {}

    void OnLateUpdate(Csm::CubismModel *model,
        Csm::csmFloat32 delta_seconds) override {
        (void)model;
        (void)delta_seconds;
        owner_.apply_parameter_overrides();
    }

private:
    NativeModel &owner_;
};

void NativeModel::add_parameter_override_updater() {
    _updateScheduler.AddUpdatableList(
        CSM_NEW ParameterOverrideUpdater(*this));
}

void NativeModel::capture_parameter_baseline() {
    if (!_model) return;
    const int count = _model->GetParameterCount();
    parameter_baseline_values_.resize((size_t)count);
    for (int i = 0; i < count; ++i)
        parameter_baseline_values_[(size_t)i] = _model->GetParameterValue(i);
}

void NativeModel::save_parameters() {
    if (!_model) return;
    if (!parameter_overrides_applied_) {
        capture_parameter_baseline();
        _model->SaveParameters();
        return;
    }
    /* Cubism's saved state is the motion baseline. Never let frame-end input
       overrides become the starting values for a later motion frame. */
    const int count = _model->GetParameterCount();
    auto &current = parameter_save_scratch_;
    current.resize((size_t)count);
    for (int i = 0; i < count; ++i) {
        current[(size_t)i] = _model->GetParameterValue(i);
        _model->SetParameterValue(i, parameter_baseline_values_[(size_t)i]);
    }
    _model->SaveParameters();
    for (int i = 0; i < count; ++i)
        _model->SetParameterValue(i, current[(size_t)i]);
}

void NativeModel::apply_parameter_overrides() {
    if (!_model) return;
    const int count = _model->GetParameterCount();
    for (int i = 0; i < count; ++i) {
        if (!parameter_overrides_[(size_t)i]) continue;
        _model->SetParameterValue(i, parameter_override_values_[(size_t)i]);
    }
    parameter_overrides_applied_ = true;
}

} // namespace bongo_cat
