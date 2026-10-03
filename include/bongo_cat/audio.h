#ifndef BONGO_CAT_AUDIO_H
#define BONGO_CAT_AUDIO_H

#include "bongo_cat/common.h"

typedef struct BongoCatAudio BongoCatAudio;

BongoCatAudio *bongo_cat_audio_create(BongoCatError *error);
void bongo_cat_audio_destroy(BongoCatAudio *audio);
BongoCatResult bongo_cat_audio_play(BongoCatAudio *audio, const char *path, BongoCatError *error);
/* With overlap disabled, only the same clip restarts; other clips continue. */
BongoCatResult bongo_cat_audio_play_sound(BongoCatAudio *audio, const char *path,
    bool overlap, BongoCatError *error);
void bongo_cat_audio_update(BongoCatAudio *audio);
void bongo_cat_audio_reset(BongoCatAudio *audio);
void bongo_cat_audio_stop(BongoCatAudio *audio);
bool bongo_cat_audio_any_playing(const BongoCatAudio *audio);
bool bongo_cat_audio_is_playing(const BongoCatAudio *audio, const char *path);
void bongo_cat_audio_stop_path(BongoCatAudio *audio, const char *path);
void bongo_cat_audio_set_enabled(BongoCatAudio *audio, bool enabled);

#endif
