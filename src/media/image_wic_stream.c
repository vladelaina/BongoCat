#include "image_internal.h"

#ifdef _WIN32
#define COBJMACROS
#include <SDL3/SDL.h>
#include <limits.h>
#include <objbase.h>
#include <stdlib.h>
#include <wincodec.h>
#include <windows.h>

typedef struct WicRowJob {
    const char *path;
    BongoCatImageRows consume;
    void *consumer;
    BongoCatImageProgress progress;
    void *userdata;
    SDL_Semaphore *ready, *consumed;
    BongoCatImage rows;
    int height, y;
    bool keep_decoding, finished, result;
} WicRowJob;

static int SDLCALL decode_rows(void *userdata) {
    WicRowJob *job = userdata;
    HRESULT initialized = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool uninitialize = SUCCEEDED(initialized);
    HRESULT status = initialized == RPC_E_CHANGED_MODE ? S_OK : initialized;
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        job->path, -1, NULL, 0);
    wchar_t *wide = count > 0 ? malloc((size_t)count * sizeof(*wide)) : NULL;
    if (!wide || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        job->path, -1, wide, count)) status = E_FAIL;
    if (SUCCEEDED(status))
        status = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
            CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(status))
        status = IWICImagingFactory_CreateDecoderFromFilename(factory, wide,
            NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(status)) status = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    UINT width = 0, height = 0;
    if (SUCCEEDED(status)) status = IWICBitmapFrameDecode_GetSize(frame, &width, &height);
    if (!width || !height || width > INT_MAX / 4 || height > INT_MAX) status = E_FAIL;
    if (SUCCEEDED(status))
        status = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(status))
        status = IWICFormatConverter_Initialize(converter,
            (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);

    /* One strip crosses the worker/caller boundary. The decoder cannot reuse
       it until the GL upload has consumed it. At 8192 pixels this is 2 MiB,
       instead of another complete 256 MiB atlas. */
    UINT stride = width * 4;
    UINT batch = stride ? SDL_min(height,
        SDL_max(1u, SDL_min(64u, 4u * 1024u * 1024u / stride))) : 0;
    unsigned char *pixels = SUCCEEDED(status) ? malloc((size_t)stride * batch) : NULL;
    if (!pixels) status = E_OUTOFMEMORY;
    for (UINT y = 0; SUCCEEDED(status) && y < height; y += batch) {
        UINT rows = SDL_min(batch, height - y);
        WICRect rect = {0, (INT)y, (INT)width, (INT)rows};
        status = IWICFormatConverter_CopyPixels(converter, &rect,
            stride, stride * rows, pixels);
        if (FAILED(status)) break;
        job->rows = (BongoCatImage){.pixels = pixels,
            .width = (int)width, .height = (int)rows};
        job->height = (int)height;
        job->y = (int)y;
        if (job->ready) {
            SDL_SignalSemaphore(job->ready);
            SDL_WaitSemaphore(job->consumed);
        } else {
            job->keep_decoding = job->consume(job->consumer,
                &job->rows, job->height, job->y);
            if (job->progress)
                job->progress(job->userdata, (float)(y + rows) / height);
        }
        if (!job->keep_decoding) { status = E_ABORT; break; }
    }
    free(pixels);
    if (converter) IWICFormatConverter_Release(converter);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    free(wide);
    if (uninitialize) CoUninitialize();
    job->result = SUCCEEDED(status);
    job->finished = true;
    if (job->ready) SDL_SignalSemaphore(job->ready);
    return job->result ? 0 : -1;
}

bool bongo_cat_image_decode_wic_rows(const char *path,
    BongoCatImageRows consume, void *consumer,
    BongoCatImageProgress progress, void *userdata) {
    WicRowJob job = {.path = path, .consume = consume, .consumer = consumer,
        .progress = progress, .userdata = userdata, .keep_decoding = true};
    job.ready = SDL_CreateSemaphore(0);
    job.consumed = SDL_CreateSemaphore(0);
    SDL_Thread *worker = job.ready && job.consumed ? SDL_CreateThread(decode_rows,
        BONGO_CAT_SLUG "-wic-rows", &job) : NULL;
    if (!worker) {
        SDL_DestroySemaphore(job.ready);
        SDL_DestroySemaphore(job.consumed);
        job.ready = job.consumed = NULL;
        decode_rows(&job);
        return job.result;
    }
    float fraction = 0.0f;
    for (;;) {
        if (SDL_WaitSemaphoreTimeout(job.ready, 2)) {
            if (job.finished) break;
            job.keep_decoding = consume(consumer, &job.rows, job.height, job.y);
            fraction = (float)(job.y + job.rows.height) / job.height;
            SDL_SignalSemaphore(job.consumed);
        }
        /* UI work and all GL calls remain on the caller's context. */
        if (progress) progress(userdata, fraction);
    }
    SDL_WaitThread(worker, NULL);
    SDL_DestroySemaphore(job.ready);
    SDL_DestroySemaphore(job.consumed);
    return job.result;
}
#endif
