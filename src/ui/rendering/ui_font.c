#include "ui_font.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static bool readable(const char *path) {
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

const char *bongo_cat_ui_system_font(char *path, size_t capacity, bool multilingual) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        const char *multi[] = {"Fonts/msyh.ttc", "Fonts/msyhl.ttc"};
        const char *latin[] = {"Fonts/segoeui.ttf", "Fonts/msyhl.ttc"};
        const char **candidates = multilingual ? multi : latin;
        for (size_t i = 0; i < 2; ++i) {
            bongo_cat_path_join(path, capacity, windows, candidates[i]);
            if (readable(path)) return path;
        }
    }
#elif defined(__APPLE__)
    const char *candidates[] = {multilingual ? "/System/Library/Fonts/PingFang.ttc" :
        "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
#else
    /* Every Linux distribution ships fonts to different directories, so the
       candidate lists must cover the common layouts. CJK-capable fonts come
       first for multilingual fallbacks; the CJK-less DejaVu remains last as a
       final resort so Chinese text never silently falls back to it. */
    static const char *const cjk[] = {
        /* Arch/Manjaro and openSUSE: noto-fonts-cjk */
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        /* Fedora/RHEL: google-noto-sans-cjk-fonts */
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
        /* Debian/Ubuntu: fonts-noto-cjk (and language-specific extras) */
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJKtc-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansSC-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.otf",
        /* WenQuanYi micro hei/zen hei (Debian, Arch, openSUSE) */
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        /* Older Ubuntu: droid fallback */
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"};
    static const char *const latin[] = {
        /* Debian/Ubuntu, Arch, Fedora ttf-dejavu layouts */
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/freefont/FreeSans.ttf"};
    const char *const *candidates = multilingual ? cjk : latin;
    size_t count = multilingual ?
        sizeof(cjk) / sizeof(cjk[0]) : sizeof(latin) / sizeof(latin[0]);
    for (size_t i = 0; i < count; ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
    if (multilingual)
        for (size_t i = 0; i < sizeof(latin) / sizeof(latin[0]); ++i) {
            if (!readable(latin[i])) continue;
            snprintf(path, capacity, "%s", latin[i]);
            return path;
        }
#endif
    return NULL;
}

const char *bongo_cat_ui_system_heading_font(char *path, size_t capacity,
    bool multilingual) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        const char *multi[] = {"Fonts/msyhbd.ttc", "Fonts/msyhl.ttc"};
        const char *latin[] = {"Fonts/seguisb.ttf", "Fonts/segoeui.ttf"};
        const char **candidates = multilingual ? multi : latin;
        for (size_t i = 0; i < 2; ++i) {
            bongo_cat_path_join(path, capacity, windows, candidates[i]);
            if (readable(path)) return path;
        }
    }
#elif defined(__APPLE__)
    const char *candidates[] = {multilingual ? "/System/Library/Fonts/PingFang.ttc" :
        "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
#else
    static const char *const cjk[] = {
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Bold.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJKtc-Bold.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.otf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"};
    static const char *const latin[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans-Bold.ttf",
        "/usr/share/fonts/freefont/FreeSans-Bold.ttf"};
    const char *const *candidates = multilingual ? cjk : latin;
    size_t count = multilingual ?
        sizeof(cjk) / sizeof(cjk[0]) : sizeof(latin) / sizeof(latin[0]);
    for (size_t i = 0; i < count; ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
    if (multilingual)
        for (size_t i = 0; i < sizeof(latin) / sizeof(latin[0]); ++i) {
            if (!readable(latin[i])) continue;
            snprintf(path, capacity, "%s", latin[i]);
            return path;
        }
#endif
    return NULL;
}

const char *bongo_cat_ui_system_korean_font(char *path, size_t capacity) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        const char *candidates[] = {"Fonts/malgun.ttf", "Fonts/malgunsl.ttf"};
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
            bongo_cat_path_join(path, capacity, windows, candidates[i]);
            if (readable(path)) return path;
        }
    }
#elif defined(__APPLE__)
    const char *candidates[] = {
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/Supplemental/AppleGothic.ttf"};
#else
    const char *candidates[] = {
        /* NotoSansCJK covers Korean on every major distribution */
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        /* Language-specific Noto Sans KR (Debian/Ubuntu, Fedora) */
        "/usr/share/fonts/opentype/noto/NotoSansKR-Regular.otf",
        "/usr/share/fonts/google-noto-sans-kr-fonts/NotoSansKR-Regular.otf",
        /* Nanum Gothic (Debian/Ubuntu, Arch, Fedora) */
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
        "/usr/share/fonts/nanum/NanumGothic.ttf",
        "/usr/share/fonts/nanum-gothic-fonts/NanumGothic.ttf"};
#endif
#ifndef _WIN32
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
#endif
    return bongo_cat_ui_system_font(path, capacity, true);
}

const char *bongo_cat_ui_system_korean_heading_font(char *path,
    size_t capacity) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        const char *candidates[] = {"Fonts/malgunbd.ttf", "Fonts/malgun.ttf"};
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
            bongo_cat_path_join(path, capacity, windows, candidates[i]);
            if (readable(path)) return path;
        }
    }
#elif defined(__APPLE__)
    const char *candidates[] = {
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/Supplemental/AppleGothic.ttf"};
#else
    const char *candidates[] = {
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansKR-Bold.otf",
        "/usr/share/fonts/google-noto-sans-kr-fonts/NotoSansKR-Bold.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf",
        "/usr/share/fonts/nanum/NanumGothicBold.ttf",
        "/usr/share/fonts/nanum-gothic-fonts/NanumGothicBold.ttf",
        /* Regular weight as a last resort for headings with no bold face */
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"};
#endif
#ifndef _WIN32
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
#endif
    return bongo_cat_ui_system_heading_font(path, capacity, true);
}
