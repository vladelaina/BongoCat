#ifndef BONGO_CAT_AUDIO_INTERNAL_H
#define BONGO_CAT_AUDIO_INTERNAL_H

#include "bongo_cat/audio.h"
#include <miniaudio.h>

#define AUDIO_VOICES 50
#define AUDIO_IDLE_MS 5000

typedef struct AudioVoice {
    ma_sound sound;
    char path[BONGO_CAT_PATH_CAP];
    uint64_t used;
    uint64_t order;
    bool ready;
} AudioVoice;

struct BongoCatAudio {
    ma_engine engine;
    AudioVoice voices[AUDIO_VOICES];
    uint64_t sequence;
    uint64_t last_play;
    uint64_t next_collect;
    bool initialized;
    bool enabled;
};

void bongo_cat_audio_voice_release(AudioVoice *voice);

/* Explicit time keeps idle reclamation deterministic in offline tests. */
void bongo_cat_audio_collect(BongoCatAudio *audio, uint64_t now_ms);

#endif
