#include "cubism_model.hpp"

#include <Motion/CubismMotionQueueEntry.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <utility>

namespace bongo_cat {

bool motion_run_clears_selection(bool one_shot, bool committed,
    bool replacement_running) {
    return one_shot && committed && !replacement_running;
}

bool NativeModel::start_motion(const char *group, int index) {
    if (!group || index < 0) return false;
    std::string key = std::string(group) + "_" + std::to_string(index);
    bool selected = false;
    std::string playback = motion_to_play(key, &selected);
    if (playback.empty()) {
        auto owner = motion_toggle_owners_.find(key);
        const std::string &canonical = owner == motion_toggle_owners_.end() ?
            key : owner->second;
        if (!restore_motion_defaults(canonical)) return false;
        select_motion(canonical, false);
        return true;
    }
    auto found = motions_.find(playback);
    if (found == motions_.end()) return false;
    constexpr int priority = 2;
    Csm::CubismMotionQueueEntryHandle handle =
        _motionManager->StartMotionPriority(found->second, false, priority);
    if (handle == Csm::InvalidMotionQueueEntryHandleValue) return false;
    select_motion(key, selected);
    record_motion_run(handle, key, selected, true);
    return true;
}

void NativeModel::record_motion_run(Csm::CubismMotionQueueEntryHandle handle,
    const std::string &key, bool selected, bool committed) {
    auto owner = motion_toggle_owners_.find(key);
    const std::string &canonical = owner == motion_toggle_owners_.end() ?
        key : owner->second;
    MotionRun run;
    run.handle = handle;
    run.key = canonical;
    run.one_shot = !motion_is_persistent(canonical);
    run.selected = selected;
    run.committed = committed;
    motion_runs_.push_back(std::move(run));
}

void NativeModel::stop_motion_runs(const std::string &key) {
    for (const MotionRun &run : motion_runs_) {
        if (run.key != key) continue;
        auto *entry = _motionManager->GetCubismMotionQueueEntry(run.handle);
        if (entry) entry->IsFinished(true);
    }
}

void NativeModel::expire_motion_runs() {
    if (motion_runs_.empty()) return;
    auto &finished = motion_finished_scratch_;
    finished.resize(motion_runs_.size());
    for (size_t i = 0; i < motion_runs_.size(); ++i)
        finished[i] = _motionManager->IsFinished(motion_runs_[i].handle);
    bool restored_persistent_state = false;
    for (size_t i = 0; i < motion_runs_.size(); ++i) {
        const MotionRun &run = motion_runs_[i];
        if (!finished[i]) continue;
        if (!run.committed) {
            if (motion_preview_active_ && run.key == motion_preview_key_)
                motion_preview_completed_ = true;
            continue;
        }
        if (!run.one_shot) {
            bool currently_selected = selected_motion_keys_.find(run.key) !=
                selected_motion_keys_.end();
            if (currently_selected == run.selected) {
                auto state = motion_states_.find(run.key);
                if (state != motion_states_.end())
                    for (const auto &curve : state->second.curves)
                        restored_persistent_state = apply_motion_curve(curve,
                            run.selected ? curve.end : curve.normal) ||
                            restored_persistent_state;
            }
        }
        bool replacement_running = false;
        if (run.one_shot) {
            auto source_signature = motion_signatures_.find(run.key);
            for (size_t j = 0; j < motion_runs_.size(); ++j) {
                auto replacement_signature = motion_signatures_.find(
                    motion_runs_[j].key);
                if (j != i && !finished[j] && motion_runs_[j].committed &&
                    (motion_runs_[j].key == run.key ||
                    (source_signature != motion_signatures_.end() &&
                    !source_signature->second.empty() &&
                    replacement_signature != motion_signatures_.end() &&
                    source_signature->second == replacement_signature->second))) {
                    replacement_running = true;
                    break;
                }
            }
        }
        if (motion_run_clears_selection(run.one_shot, run.committed,
            replacement_running)) select_motion(run.key, false);
    }
    if (restored_persistent_state) {
        save_parameters();
    }
    size_t output = 0;
    for (size_t i = 0; i < motion_runs_.size(); ++i)
        if (!finished[i]) motion_runs_[output++] = std::move(motion_runs_[i]);
    motion_runs_.resize(output);
}

void NativeModel::clear_motion_runs() {
    for (const MotionRun &run : motion_runs_)
        if (run.committed && run.one_shot) select_motion(run.key, false);
    motion_runs_.clear();
}

bool NativeModel::motion_selected(const char *group, int index) const {
    if (!group || index < 0) return false;
    std::string key = std::string(group) + "_" + std::to_string(index);
    auto owner = motion_toggle_owners_.find(key);
    if (owner != motion_toggle_owners_.end()) key = owner->second;
    return selected_motion_keys_.find(key) != selected_motion_keys_.end();
}

bool NativeModel::motion_visible(const char *group, int index) const {
    if (!group || index < 0) return false;
    std::string key = std::string(group) + "_" + std::to_string(index);
    auto owner = motion_toggle_owners_.find(key);
    return owner == motion_toggle_owners_.end() || owner->second == key;
}

bool NativeModel::motion_same_toggle(const char *left_group, int left_index,
    const char *right_group, int right_index) const {
    if (!left_group || !right_group || left_index < 0 || right_index < 0)
        return false;
    std::string left = std::string(left_group) + "_" +
        std::to_string(left_index);
    std::string right = std::string(right_group) + "_" +
        std::to_string(right_index);
    auto left_owner = motion_toggle_owners_.find(left);
    auto right_owner = motion_toggle_owners_.find(right);
    if (left_owner == motion_toggle_owners_.end() ||
        right_owner == motion_toggle_owners_.end()) return false;
    return left_owner->second == right_owner->second;
}

} // namespace bongo_cat
