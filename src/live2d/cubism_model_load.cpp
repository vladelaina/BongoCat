#include "cubism_model.hpp"
#include "cubism_viewer_look.hpp"
#include "bongo_cat/file.h"
#include "bongo_cat/image.h"
#include "bongo_cat/json.h"

#include <Effect/CubismBreath.hpp>
#include <Effect/CubismEyeBlink.hpp>
#include <Id/CubismIdManager.hpp>
#include <Motion/CubismExpressionUpdater.hpp>
#include <Motion/CubismBreathUpdater.hpp>
#include <Motion/CubismExpressionMotionManager.hpp>
#include <Motion/CubismEyeBlinkUpdater.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismPhysicsUpdater.hpp>
#include <Motion/CubismPoseUpdater.hpp>
#include <Motion/ICubismUpdater.hpp>
#include <Physics/CubismPhysics.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

namespace bongo_cat {

NativeModel::NativeModel() {
    _mocConsistency = true;
    _motionConsistency = true;
}

NativeModel::~NativeModel() {
    _motionManager->StopAllMotions();
    _expressionManager->StopAllMotions();
    for (auto &item : motions_) Csm::ACubismMotion::Delete(item.second);
    for (auto &item : expressions_) Csm::ACubismMotion::Delete(item.second);
    release_render_resources();
    delete setting_;
}

std::vector<unsigned char> NativeModel::read(const std::string &file, size_t maximum) const {
    FILE *stream = bongo_cat_file_open(file.c_str(), "rb");
    if (!stream || std::fseek(stream, 0, SEEK_END) != 0) {
        if (stream) std::fclose(stream);
        return {};
    }
    long size = std::ftell(stream);
    if (size <= 0 || (size_t)size > maximum || std::fseek(stream, 0, SEEK_SET) != 0) {
        std::fclose(stream);
        return {};
    }
    std::vector<unsigned char> bytes((size_t)size);
    bool read = std::fread(bytes.data(), 1, (size_t)size, stream) == (size_t)size;
    std::fclose(stream);
    if (!read) return {};
    return bytes;
}

std::string NativeModel::path(const char *relative) const {
    return directory_ + (relative ? relative : "");
}

bool NativeModel::load(const char *directory, const char *setting_file,
    bool direct_textures, bool dynamic_texture_resolution,
    BongoCatLive2DLoadProgress progress, void *userdata, BongoCatError *error) {
    if (!directory || !setting_file) return false;
    visual_state_ready_ = false;
    visual_state_ = BongoCatLive2DVisualState{};
    direct_textures_ = direct_textures;
    dynamic_texture_resolution_ = dynamic_texture_resolution;
    directory_ = directory;
    if (!directory_.empty() && directory_.back() != '/' && directory_.back() != '\\')
        directory_ += '/';
    std::vector<unsigned char> json = read(path(setting_file), 4 * 1024 * 1024);
    if (json.empty()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot read model setting: %s", setting_file);
        return false;
    }
    bool normalized = false;
    yyjson_doc *document = bongo_cat_model_json_parse(
        reinterpret_cast<const char *>(json.data()), json.size(), &normalized);
    if (document && normalized) {
        size_t size = 0;
        char *canonical = yyjson_write(document, 0, &size);
        if (!canonical) {
            yyjson_doc_free(document);
            bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY,
                "Cannot prepare model setting: %s", setting_file);
            return false;
        }
        json.assign(canonical, canonical + size);
        std::free(canonical);
    }
    yyjson_doc_free(document);
    if (!validate_model_setting_json(json, setting_file, error)) return false;
    if (progress) progress(userdata, .10f);
    setting_ = new(std::nothrow)
        Csm::CubismModelSettingJson(json.data(), (Csm::csmSizeInt)json.size());
    if (!setting_) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY, "Cannot allocate model setting");
        return false;
    }
    if (!load_model(error)) return false;
    if (progress) progress(userdata, .25f);
    load_expressions();
    if (progress) progress(userdata, .31f);
    load_effects();
    if (progress) progress(userdata, .35f);
    load_motions(progress, userdata);
    Csm::csmMap<Csm::csmString, Csm::csmFloat32> layout;
    setting_->GetLayoutMap(layout);
    _modelMatrix->SetupFromLayout(layout);
    save_parameters();
    if (progress) progress(userdata, .49f);
    return true;
}
bool NativeModel::load_model(BongoCatError *error) {
    const char *name = setting_->GetModelFileName();
    std::vector<unsigned char> bytes = read(path(name));
    if (bytes.empty()) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot read moc3: %s", name);
        return false;
    }
    LoadModel(bytes.data(), (Csm::csmSizeInt)bytes.size(), true);
    if (!_model) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_CUBISM, "Cubism rejected moc3: %s", name);
        return false;
    }
    const size_t parameter_count = (size_t)_model->GetParameterCount();
    parameter_override_values_.assign(parameter_count, 0.0f);
    parameter_baseline_values_.resize(parameter_count);
    parameter_overrides_.assign(parameter_count, 0);
    for (size_t i = 0; i < parameter_count; ++i)
        parameter_baseline_values_[i] = _model->GetParameterValue((int)i);
    parameter_overrides_applied_ = false;
    return true;
}
void NativeModel::load_expressions() {
    expression_names_.resize((size_t)setting_->GetExpressionCount());
    for (int i = 0; i < setting_->GetExpressionCount(); ++i) {
        const char *name = setting_->GetExpressionName(i);
        std::vector<unsigned char> bytes = read(path(setting_->GetExpressionFileName(i)));
        if (bytes.empty()) continue;
        Csm::ACubismMotion *motion = LoadExpression(bytes.data(),
            (Csm::csmSizeInt)bytes.size(), name);
        if (!motion) continue;
        std::string key = std::to_string(i);
        expressions_[key] = motion;
        expression_names_[(size_t)i] = key;
    }
    if (!expressions_.empty())
        _updateScheduler.AddUpdatableList(
            CSM_NEW Csm::CubismExpressionUpdater(*_expressionManager));
}

void NativeModel::load_effects() {
    if (setting_->GetPhysicsFileName()[0]) {
        auto bytes = read(path(setting_->GetPhysicsFileName()));
        if (!bytes.empty()) LoadPhysics(bytes.data(), (Csm::csmSizeInt)bytes.size());
        if (_physics) _updateScheduler.AddUpdatableList(
            CSM_NEW Csm::CubismPhysicsUpdater(*_physics));
    }
    if (setting_->GetPoseFileName()[0]) {
        auto bytes = read(path(setting_->GetPoseFileName()));
        if (!bytes.empty()) LoadPose(bytes.data(), (Csm::csmSizeInt)bytes.size());
        if (_pose) _updateScheduler.AddUpdatableList(CSM_NEW Csm::CubismPoseUpdater(*_pose));
    }
    if (setting_->GetUserDataFile()[0]) {
        auto bytes = read(path(setting_->GetUserDataFile()));
        if (!bytes.empty()) LoadUserData(bytes.data(), (Csm::csmSizeInt)bytes.size());
    }
    viewer_look_ = CSM_NEW ViewerLookUpdater(*_model);
    _updateScheduler.AddUpdatableList(viewer_look_);
    _breath = Csm::CubismBreath::Create();
    if (_breath) {
        Csm::csmVector<Csm::CubismBreath::BreathParameterData> parameters;
        Csm::CubismIdManager *ids = Csm::CubismFramework::GetIdManager();
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamAngleX"), 0.0f, 15.0f, 6.5345f, 0.5f));
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamAngleY"), 0.0f, 8.0f, 3.5345f, 0.5f));
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamAngleZ"), 0.0f, 10.0f, 5.5345f, 0.5f));
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamBodyAngleX"), 0.0f, 4.0f, 15.5345f, 0.5f));
        parameters.PushBack(Csm::CubismBreath::BreathParameterData(
            ids->GetId("ParamBreath"), 0.5f, 0.5f, 3.2345f, 0.5f));
        _breath->SetParameters(parameters);
        _updateScheduler.AddUpdatableList(
            CSM_NEW Csm::CubismBreathUpdater(*_breath));
    }
    add_parameter_override_updater();
    for (int i = 0; i < setting_->GetEyeBlinkParameterCount(); ++i)
        eye_blink_ids_.PushBack(setting_->GetEyeBlinkParameterId(i));
    for (int i = 0; i < setting_->GetLipSyncParameterCount(); ++i)
        lip_sync_ids_.PushBack(setting_->GetLipSyncParameterId(i));
    if (setting_->GetEyeBlinkParameterCount() > 0) {
        _eyeBlink = Csm::CubismEyeBlink::Create(setting_);
        _updateScheduler.AddUpdatableList(
            CSM_NEW Csm::CubismEyeBlinkUpdater(motion_updated_, *_eyeBlink));
    }
    _updateScheduler.SortUpdatableList();
}

void NativeModel::load_motions(BongoCatLive2DLoadProgress progress,
    void *userdata) {
    idle_motion_keys_.clear();
    motion_signatures_.clear();
    motion_states_.clear();
    motion_toggle_partners_.clear();
    motion_toggle_owners_.clear();
    selected_motion_keys_.clear();
    clear_motion_runs();
    int total = 0, completed = 0;
    for (int group_index = 0; group_index < setting_->GetMotionGroupCount();
        ++group_index)
        total += setting_->GetMotionCount(setting_->GetMotionGroupName(group_index));
    for (int group_index = 0; group_index < setting_->GetMotionGroupCount(); ++group_index) {
        const char *group = setting_->GetMotionGroupName(group_index);
        for (int i = 0; i < setting_->GetMotionCount(group); ++i) {
            if (progress) progress(userdata, .35f + .14f *
                (float)(++completed) / (float)(total > 0 ? total : 1));
            auto bytes = read(path(setting_->GetMotionFileName(group, i)));
            if (bytes.empty()) continue;
            std::string key = std::string(group) + "_" + std::to_string(i);
            auto *motion = static_cast<Csm::CubismMotion *>(LoadMotion(bytes.data(),
                (Csm::csmSizeInt)bytes.size(), key.c_str(), nullptr, nullptr,
                setting_, group, i, _motionConsistency));
            if (!motion) continue;
            motion->SetEffectIds(eye_blink_ids_, lip_sync_ids_);
            motions_[key] = motion;
            load_motion_state(key, group, i, bytes);
            if (std::strcmp(group, "Idle") == 0) idle_motion_keys_.push_back(key);
        }
    }
    _motionManager->StopAllMotions();
}

} // namespace bongo_cat
