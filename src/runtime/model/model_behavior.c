#include "runtime.h"
#include "bongo_cat/audio.h"
#include "bongo_cat/overlay.h"

bool bongo_cat_app_run_behavior(BongoCatApp *app,
    const BongoCatBehaviorEntry *behavior) {
    if (!app || !behavior) return false;
    if (behavior->kind == BONGO_CAT_BEHAVIOR_EFFECT) {
        if (!bongo_cat_overlay_effect(app->overlay, behavior->effect)) return false;
    } else if (behavior->kind == BONGO_CAT_BEHAVIOR_SOUND) {
        if (behavior->sound_clear) {
            bongo_cat_audio_stop(app->audio);
            return true;
        }
        if (!behavior->sound[0]) return false;
        BongoCatError error = {0};
        if (bongo_cat_audio_play_sound(app->audio, behavior->sound,
            behavior->sound_overlap, &error) != BONGO_CAT_OK) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "%s", error.message);
            return false;
        }
        return true;
    } else if (behavior->kind == BONGO_CAT_BEHAVIOR_MOTION) {
        bool started = bongo_cat_live2d_start_motion(app->live2d,
            behavior->group, behavior->index);
        if (!started) return false;
        if (behavior->sound[0]) {
            BongoCatError error = {0};
            bongo_cat_audio_play(app->audio, behavior->sound, &error);
        }
    } else {
        int expression = bongo_cat_live2d_expression(app->live2d) ==
            behavior->index ? -1 : behavior->index;
        if (!bongo_cat_live2d_set_expression(app->live2d, expression)) return false;
    }
    bongo_cat_app_capture_behavior_state(app);
    app->dirty = true;
    return true;
}

