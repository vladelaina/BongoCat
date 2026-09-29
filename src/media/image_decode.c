#include "image_internal.h"

#include "bongo_cat/file.h"

#include <SDL3/SDL.h>
#include <stb_image.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define COBJMACROS
#include <objbase.h>
#include <wincodec.h>
#include <windows.h>
#endif

BongoCatResult bongo_cat_image_decode_pixels(const char *path,
    BongoCatImage *image, BongoCatError *error) {
    if (!path || !image) return BONGO_CAT_ERROR_ARGUMENT;
    memset(image, 0, sizeof(*image));
    int channels;
    FILE *file = bongo_cat_file_open(path, "rb");
    image->pixels = file ? stbi_load_from_file(file, &image->width,
        &image->height, &channels, STBI_rgb_alpha) : NULL;
    image->pixels_stbi = image->pixels != NULL;
    if (file) fclose(file);
    if (image->pixels) return BONGO_CAT_OK;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_IO,
        "Cannot decode image: %s", path);
    return BONGO_CAT_ERROR_IO;
}

typedef struct PixelDecodeJob {
    const char *path;
    BongoCatImage *image;
    BongoCatError *error;
    BongoCatResult result;
} PixelDecodeJob;

static int SDLCALL decode_pixels_worker(void *userdata) {
    /* Workers only produce CPU pixels; Cubism and OpenGL remain on the caller. */
    PixelDecodeJob *job = userdata;
    job->result = bongo_cat_image_decode_pixels(job->path,
        job->image, job->error);
    return (int)job->result;
}

static void wait_for_decode(SDL_Thread *worker,
    BongoCatImageProgress progress, void *userdata) {
    while (SDL_GetThreadState(worker) == SDL_THREAD_ALIVE) {
        if (progress) progress(userdata, .5f);
        SDL_Delay(2);
    }
    SDL_WaitThread(worker, NULL);
    if (progress) progress(userdata, 1.0f);
}

BongoCatResult bongo_cat_image_decode_pixels_responsive(const char *path,
    BongoCatImage *image, BongoCatImageProgress progress, void *userdata,
    BongoCatError *error) {
    if (!progress)
        return bongo_cat_image_decode_pixels(path, image, error);
    PixelDecodeJob job = {path, image, error, BONGO_CAT_ERROR_PLATFORM};
    SDL_Thread *worker = SDL_CreateThread(decode_pixels_worker,
        BONGO_CAT_SLUG "-image-decode", &job);
    if (!worker)
        return bongo_cat_image_decode_pixels(path, image, error);
    wait_for_decode(worker, progress, userdata);
    return job.result;
}

#ifdef _WIN32
static wchar_t *wide_path(const char *path) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        path, -1, NULL, 0);
    wchar_t *wide = count > 0 ? malloc((size_t)count * sizeof(*wide)) : NULL;
    if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        path, -1, wide, count)) {
        free(wide);
        return NULL;
    }
    return wide;
}

static bool decode_wic(const char *path, BongoCatImage *image,
    UINT max_width, UINT max_height, BongoCatImageProgress progress,
    void *userdata, BongoCatImageCancelled cancelled, void *cancel_data) {
    memset(image, 0, sizeof(*image));
    if (cancelled && cancelled(cancel_data)) return false;
    HRESULT initialized = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool uninitialize = initialized == S_OK || initialized == S_FALSE;
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *premultiplied = NULL;
    IWICFormatConverter *converter = NULL;
    wchar_t *wide = wide_path(path);
    HRESULT result = wide ? CoCreateInstance(&CLSID_WICImagingFactory, NULL,
        CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
        (void **)&factory) : E_FAIL;
    if (SUCCEEDED(result))
        result = IWICImagingFactory_CreateDecoderFromFilename(factory, wide,
            NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(result)) result = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    UINT source_width = 0, source_height = 0;
    if (SUCCEEDED(result))
        result = IWICBitmapFrameDecode_GetSize(frame,
            &source_width, &source_height);
    if (!source_width || !source_height || source_width > INT_MAX / 4 ||
        source_height > INT_MAX) result = E_FAIL;
    if (cancelled && cancelled(cancel_data)) result = E_ABORT;
    UINT target_width = source_width, target_height = source_height;
    if (max_width && max_height &&
        (source_width > max_width || source_height > max_height)) {
        if ((uint64_t)max_width * source_height <=
            (uint64_t)max_height * source_width) {
            target_width = max_width;
            target_height = (UINT)((uint64_t)source_height * max_width /
                source_width);
        } else {
            target_height = max_height;
            target_width = (UINT)((uint64_t)source_width * max_height /
                source_height);
        }
        if (!target_width) target_width = 1;
        if (!target_height) target_height = 1;
        /* Keep transparent RGB out of the resize filter. Only the output is
           explicitly bounded here; the codec may retain source storage. */
        if (SUCCEEDED(result))
            result = IWICImagingFactory_CreateFormatConverter(factory, &premultiplied);
        if (SUCCEEDED(result))
            result = IWICFormatConverter_Initialize(premultiplied,
                (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppPRGBA,
                WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(result))
            result = IWICImagingFactory_CreateBitmapScaler(factory, &scaler);
        if (SUCCEEDED(result))
            result = IWICBitmapScaler_Initialize(scaler,
                (IWICBitmapSource *)premultiplied, target_width, target_height,
                WICBitmapInterpolationModeFant);
    }
    IWICBitmapSource *source = scaler ? (IWICBitmapSource *)scaler :
        (IWICBitmapSource *)frame;
    if (SUCCEEDED(result))
        result = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(result))
        result = IWICFormatConverter_Initialize(converter, source,
            &GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0,
            WICBitmapPaletteTypeCustom);
    size_t stride = target_width <= SIZE_MAX / 4
        ? (size_t)target_width * 4 : 0;
    size_t bytes = stride && target_height <= SIZE_MAX / stride
        ? stride * target_height : 0;
    if (cancelled && cancelled(cancel_data)) result = E_ABORT;
    unsigned char *pixels = SUCCEEDED(result) && bytes && bytes <= UINT_MAX
        ? malloc(bytes) : NULL;
    if (SUCCEEDED(result) && pixels) {
        for (UINT y = 0; y < target_height && SUCCEEDED(result); y += 256) {
            if (cancelled && cancelled(cancel_data)) { result = E_ABORT; break; }
            UINT rows = SDL_min(256u, target_height - y);
            WICRect rect = {0, (INT)y, (INT)target_width, (INT)rows};
            result = IWICFormatConverter_CopyPixels(converter, &rect,
                (UINT)stride, (UINT)(stride * rows),
                pixels + (size_t)y * stride);
            if (progress)
                progress(userdata,
                    (float)(y + rows) / (float)target_height);
        }
    }
    bool ok = SUCCEEDED(result) && pixels && !(cancelled && cancelled(cancel_data));
    if (ok) {
        image->pixels = pixels;
        image->width = (int)target_width;
        image->height = (int)target_height;
    } else {
        free(pixels);
    }
    if (converter) IWICFormatConverter_Release(converter);
    if (scaler) IWICBitmapScaler_Release(scaler);
    if (premultiplied) IWICFormatConverter_Release(premultiplied);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    free(wide);
    if (uninitialize) CoUninitialize();
    return ok;
}

typedef struct WicDecodeJob {
    const char *path;
    BongoCatImage *image;
    UINT max_width;
    UINT max_height;
    bool result;
} WicDecodeJob;

static int SDLCALL decode_wic_worker(void *userdata) {
    WicDecodeJob *job = userdata;
    job->result = decode_wic(job->path, job->image,
        job->max_width, job->max_height, NULL, NULL, NULL, NULL);
    return job->result ? 0 : -1;
}

bool bongo_cat_image_decode_wic_responsive(const char *path,
    BongoCatImage *image, int max_width, int max_height,
    BongoCatImageProgress progress, void *userdata) {
    if (!progress)
        return decode_wic(path, image, (UINT)max_width,
            (UINT)max_height, NULL, NULL, NULL, NULL);
    WicDecodeJob job = {path, image, (UINT)max_width, (UINT)max_height, false};
    SDL_Thread *worker = SDL_CreateThread(decode_wic_worker,
        BONGO_CAT_SLUG "-wic-decode", &job);
    if (!worker)
        return decode_wic(path, image, (UINT)max_width,
            (UINT)max_height, progress, userdata, NULL, NULL);
    wait_for_decode(worker, progress, userdata);
    return job.result;
}

bool bongo_cat_image_decode_wic_cancellable(const char *path,
    BongoCatImage *image, int max_width, int max_height,
    BongoCatImageCancelled cancelled, void *cancel_data) {
    return decode_wic(path, image, (UINT)max_width, (UINT)max_height,
        NULL, NULL, cancelled, cancel_data);
}
#endif
