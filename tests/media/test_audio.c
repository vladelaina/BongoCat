#include "audio_internal.h"
#include "bongo_cat/file.h"
#include "test.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int bongo_cat_test_failures;

static void little(FILE *file, unsigned value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) fputc((int)((value >> (i * 8)) & 255), file);
}

static bool wave(const char *path) {
    FILE *file = bongo_cat_file_open(path, "wb");
    if (!file) return false;
    unsigned frames = 96000;
    fwrite("RIFF", 1, 4, file); little(file, 36 + frames * 2, 4);
    fwrite("WAVEfmt ", 1, 8, file); little(file, 16, 4);
    little(file, 1, 2); little(file, 1, 2); little(file, 48000, 4);
    little(file, 96000, 4); little(file, 2, 2); little(file, 16, 2);
    fwrite("data", 1, 4, file); little(file, frames * 2, 4);
    for (unsigned i = 0; i < frames; ++i) little(file, (i / 100) % 2 ? 4000 : 61536, 2);
    bool ok = !ferror(file);
    return fclose(file) == 0 && ok;
}

static bool offline(BongoCatAudio *audio) {
    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.noAutoStart = MA_TRUE;
    config.channels = 2;
    config.sampleRate = 48000;
    audio->initialized = ma_engine_init(&config, &audio->engine) == MA_SUCCESS;
    return audio->initialized;
}

static size_t count(const BongoCatAudio *audio) {
    size_t total = 0;
    for (size_t i = 0; i < AUDIO_VOICES; ++i) total += audio->voices[i].ready;
    return total;
}

static bool audible(BongoCatAudio *audio) {
    uint64_t deadline = SDL_GetTicks() + 3000;
    do {
        float samples[512] = {0};
        if (ma_engine_read_pcm_frames(&audio->engine, samples, 256, NULL) != MA_SUCCESS) return false;
        for (size_t i = 0; i < 512; ++i) if (fabsf(samples[i]) > 0.001f) return true;
        SDL_Delay(1);
    } while (SDL_GetTicks() < deadline);
    return false;
}

int main(void) {
    char first[BONGO_CAT_PATH_CAP], second[BONGO_CAT_PATH_CAP];
    snprintf(first, sizeof(first), "bongocat-audio-%llu-\xE9\x9F\xB3.wav",
        (unsigned long long)SDL_GetTicksNS());
    snprintf(second, sizeof(second), "bongocat-audio-%llu.wav",
        (unsigned long long)SDL_GetTicksNS());
    CHECK(wave(first)); CHECK(wave(second));
    BongoCatError error = {0};
    BongoCatAudio *audio = bongo_cat_audio_create(&error);
    CHECK(audio != NULL);
    if (!audio) return 1;
    CHECK(!audio->initialized);
    CHECK(!bongo_cat_audio_any_playing(audio));
    CHECK(offline(audio));
    if (!audio->initialized) { bongo_cat_audio_destroy(audio); return 1; }
    CHECK(bongo_cat_audio_play_sound(audio, first, false, &error) == BONGO_CAT_OK);
    CHECK(audible(audio));
    CHECK(bongo_cat_audio_play_sound(audio, second, false, &error) == BONGO_CAT_OK);
    CHECK(count(audio) == 2);
    CHECK(bongo_cat_audio_is_playing(audio, first));
    bongo_cat_audio_stop_path(audio, first);
    CHECK(!bongo_cat_audio_is_playing(audio, first));
    CHECK(bongo_cat_audio_is_playing(audio, second));
    CHECK(bongo_cat_audio_any_playing(audio));
    CHECK(bongo_cat_audio_play_sound(audio, first, false, &error) == BONGO_CAT_OK);
    CHECK(count(audio) == 2); /* Restarting first must not discard second. */
    CHECK(audible(audio));
    for (size_t i = 0; i < AUDIO_VOICES + 10; ++i)
        CHECK(bongo_cat_audio_play_sound(audio, first, true, &error) == BONGO_CAT_OK);
    CHECK(count(audio) == AUDIO_VOICES);
    /* Stop/reset while asynchronous file loads are still pending. */
    bongo_cat_audio_stop(audio); CHECK(count(audio) == 0);
    CHECK(!bongo_cat_audio_any_playing(audio));
    CHECK(bongo_cat_audio_play(audio,
        BONGO_CAT_NATIVE_SOURCE_DIR "/resources/assets/models/standard/live2d_motion1.flac",
        &error) == BONGO_CAT_OK);
    CHECK(audible(audio));
    bongo_cat_audio_set_enabled(audio, false);
    CHECK(!audio->initialized && count(audio) == 0);
    CHECK(!bongo_cat_audio_is_playing(audio, first));
    CHECK(bongo_cat_audio_play(audio, first, &error) == BONGO_CAT_OK);
    CHECK(!audio->initialized);
    CHECK(!bongo_cat_audio_any_playing(audio));
    bongo_cat_audio_set_enabled(audio, true);
    CHECK(offline(audio));
    CHECK(bongo_cat_audio_play(audio, first, &error) == BONGO_CAT_OK);
    uint64_t end_deadline = SDL_GetTicks() + 4000;
    while (bongo_cat_audio_is_playing(audio, first) && SDL_GetTicks() < end_deadline) {
        float samples[2048];
        CHECK(ma_engine_read_pcm_frames(&audio->engine, samples, 1024, NULL) == MA_SUCCESS);
        SDL_Delay(1);
    }
    CHECK(!bongo_cat_audio_is_playing(audio, first));
    CHECK(bongo_cat_audio_play(audio, first, &error) == BONGO_CAT_OK);
    CHECK(audible(audio));
    for (size_t i = 0; i < AUDIO_VOICES; ++i)
        if (audio->voices[i].ready) ma_sound_stop(&audio->voices[i].sound);
    bongo_cat_audio_collect(audio, audio->last_play + AUDIO_IDLE_MS + 1);
    CHECK(!audio->initialized && count(audio) == 0);
    CHECK(!bongo_cat_audio_is_playing(audio, first));
    CHECK(offline(audio));
    BongoCatResult invalid = bongo_cat_audio_play(audio, "missing-audio-test-file.wav", &error);
    CHECK(invalid == BONGO_CAT_OK || invalid == BONGO_CAT_ERROR_IO);
    uint64_t deadline = SDL_GetTicks() + 3000;
    while (count(audio) && SDL_GetTicks() < deadline) {
        audio->next_collect = 0;
        bongo_cat_audio_collect(audio, SDL_GetTicks());
        SDL_Delay(1);
    }
    CHECK(count(audio) == 0);
    bongo_cat_audio_reset(audio); bongo_cat_audio_reset(audio);
    bongo_cat_audio_destroy(audio);
    CHECK(SDL_RemovePath(first)); CHECK(SDL_RemovePath(second));
    return bongo_cat_test_failures ? 1 : 0;
}
