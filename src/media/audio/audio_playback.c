#include "audio_internal.h"

#include <SDL3/SDL.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static ma_result load_voice(BongoCatAudio *audio, AudioVoice *voice, const char *path) {
    ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_ASYNC |
        MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
    wchar_t wide[BONGO_CAT_PATH_CAP];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
        wide, BONGO_CAT_PATH_CAP)) return MA_INVALID_ARGS;
    return ma_sound_init_from_file_w(&audio->engine, wide, flags, NULL, NULL, &voice->sound);
#else
    return ma_sound_init_from_file(&audio->engine, path, flags, NULL, NULL, &voice->sound);
#endif
}

BongoCatResult bongo_cat_audio_play_sound(BongoCatAudio *audio, const char *path,
    bool overlap, BongoCatError *error) {
    if (!audio || !path || !path[0] || strlen(path) >= BONGO_CAT_PATH_CAP) {
        bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT, "Invalid audio path or player");
        return BONGO_CAT_ERROR_ARGUMENT;
    }
    if (!audio->enabled) return BONGO_CAT_OK;
    if (!audio->initialized) {
        ma_result result = ma_engine_init(NULL, &audio->engine);
        if (result != MA_SUCCESS) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM,
                "Audio initialization failed: %d", result);
            return BONGO_CAT_ERROR_PLATFORM;
        }
        audio->initialized = true;
    }
    AudioVoice *selected = NULL, *idle = NULL, *oldest = &audio->voices[0];
    for (size_t i = 0; i < AUDIO_VOICES; ++i) {
        AudioVoice *voice = &audio->voices[i];
        bool playing = voice->ready && ma_sound_is_playing(&voice->sound) &&
            !ma_sound_at_end(&voice->sound);
        if (voice->ready && strcmp(voice->path, path) == 0 && (!overlap || !playing)) {
            selected = voice;
            break;
        }
        if (!playing && (!idle || !voice->ready)) idle = voice;
        if (voice->order < oldest->order) oldest = voice;
    }
    if (!selected) {
        selected = idle ? idle : oldest;
        bongo_cat_audio_voice_release(selected);
        ma_result result = load_voice(audio, selected, path);
        if (result != MA_SUCCESS) {
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
                "Cannot load audio (%d): %s", result, path);
            return BONGO_CAT_ERROR_IO;
        }
        selected->ready = true;
        memcpy(selected->path, path, strlen(path) + 1);
    } else {
        ma_sound_stop(&selected->sound);
        ma_result result = ma_sound_seek_to_pcm_frame(&selected->sound, 0);
        if (result != MA_SUCCESS) {
            bongo_cat_audio_voice_release(selected);
            bongo_cat_error_set(error, BONGO_CAT_ERROR_IO, "Cannot rewind audio: %s", path);
            return BONGO_CAT_ERROR_IO;
        }
    }
    selected->used = audio->last_play = SDL_GetTicks();
    selected->order = ++audio->sequence;
    if (ma_sound_start(&selected->sound) != MA_SUCCESS) {
        bongo_cat_audio_voice_release(selected);
        bongo_cat_error_set(error, BONGO_CAT_ERROR_PLATFORM, "Cannot start audio: %s", path);
        return BONGO_CAT_ERROR_PLATFORM;
    }
    return BONGO_CAT_OK;
}

BongoCatResult bongo_cat_audio_play(BongoCatAudio *audio, const char *path, BongoCatError *error) {
    return bongo_cat_audio_play_sound(audio, path, false, error);
}

