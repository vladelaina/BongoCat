#include "audio_internal.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* App-thread owned. Streaming bounds decoded memory independently of clip
   duration; the pool bounds decoder/file handles during rapid key presses. */
void bongo_cat_audio_voice_release(AudioVoice *voice) {
    if (!voice->ready) return;
    ma_sound_stop(&voice->sound);
    /* uninit waits for pending decoding jobs before releasing their data. */
    ma_sound_uninit(&voice->sound);
    memset(voice, 0, sizeof(*voice));
}

BongoCatAudio *bongo_cat_audio_create(BongoCatError *error) {
    BongoCatAudio *audio = calloc(1, sizeof(*audio));
    if (!audio) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_MEMORY, "Cannot allocate audio player");
        return NULL;
    }
    audio->enabled = true;
    return audio;
}

void bongo_cat_audio_stop(BongoCatAudio *audio) {
    if (!audio) return;
    for (size_t i = 0; i < AUDIO_VOICES; ++i) bongo_cat_audio_voice_release(&audio->voices[i]);
}

void bongo_cat_audio_reset(BongoCatAudio *audio) {
    if (!audio) return;
    bongo_cat_audio_stop(audio);
    if (audio->initialized) ma_engine_uninit(&audio->engine);
    audio->initialized = false;
    audio->next_collect = 0;
}

void bongo_cat_audio_collect(BongoCatAudio *audio, uint64_t now) {
    if (!audio || !audio->initialized || now < audio->next_collect) return;
    audio->next_collect = now + 1000;
    bool active = false;
    for (size_t i = 0; i < AUDIO_VOICES; ++i) {
        AudioVoice *voice = &audio->voices[i];
        if (!voice->ready) continue;
        ma_result result = ma_resource_manager_data_source_result(
            voice->sound.pResourceManagerDataSource);
        if (result != MA_SUCCESS && result != MA_BUSY) {
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "Cannot decode audio (%d): %s", result, voice->path);
            bongo_cat_audio_voice_release(voice);
        } else if (ma_sound_at_end(&voice->sound) || !ma_sound_is_playing(&voice->sound)) {
            if (now - voice->used >= AUDIO_IDLE_MS) bongo_cat_audio_voice_release(voice);
        } else active = true;
    }
    /* Idle players retain neither device callbacks nor decoder workers. */
    if (!active && now - audio->last_play >= AUDIO_IDLE_MS) bongo_cat_audio_reset(audio);
}

void bongo_cat_audio_update(BongoCatAudio *audio) {
    bongo_cat_audio_collect(audio, SDL_GetTicks());
}

void bongo_cat_audio_set_enabled(BongoCatAudio *audio, bool enabled) {
    if (!audio) return;
    audio->enabled = enabled;
    if (!enabled) bongo_cat_audio_reset(audio);
}

void bongo_cat_audio_destroy(BongoCatAudio *audio) {
    if (!audio) return;
    bongo_cat_audio_reset(audio);
    free(audio);
}

bool bongo_cat_audio_is_playing(const BongoCatAudio *audio, const char *path) {
    if (!audio || !path || !path[0]) return false;
    for (size_t i = 0; i < AUDIO_VOICES; ++i) {
        const AudioVoice *voice = &audio->voices[i];
        if (!voice->ready || strcmp(voice->path, path)) continue;
        ma_result result = ma_resource_manager_data_source_result(voice->sound.pResourceManagerDataSource);
        if ((result == MA_SUCCESS || result == MA_BUSY) &&
            ma_sound_is_playing(&voice->sound) && !ma_sound_at_end(&voice->sound)) return true;
    }
    return false;
}

void bongo_cat_audio_stop_path(BongoCatAudio *audio, const char *path) {
    if (!audio || !path || !path[0]) return;
    for (size_t i = 0; i < AUDIO_VOICES; ++i)
        if (audio->voices[i].ready && !strcmp(audio->voices[i].path, path))
            bongo_cat_audio_voice_release(&audio->voices[i]);
}

bool bongo_cat_audio_any_playing(const BongoCatAudio *audio) {
    if (!audio) return false;
    for (size_t i = 0; i < AUDIO_VOICES; ++i) {
        const AudioVoice *voice = &audio->voices[i];
        if (!voice->ready) continue;
        ma_result result = ma_resource_manager_data_source_result(voice->sound.pResourceManagerDataSource);
        if ((result == MA_SUCCESS || result == MA_BUSY) &&
            ma_sound_is_playing(&voice->sound) && !ma_sound_at_end(&voice->sound)) return true;
    }
    return false;
}
