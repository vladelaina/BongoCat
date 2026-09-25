#include "ui_font.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include <fontconfig/fontconfig.h>

#if defined(_WIN32) || defined(__APPLE__)
static bool readable(const char *path) {
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}
#endif

static bool rasterizable(const char *path) {
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    bool ok = false;
    unsigned char header[16];
    if (fread(header, 1, sizeof header, file) == sizeof header) {
        long base = 0;
        if (memcmp(header, "ttcf", 4) == 0)
            base = (long)((unsigned long)header[12] << 24
                | (unsigned long)header[13] << 16
                | (unsigned long)header[14] << 8 | header[15]);
        unsigned char count[2];
        if (fseek(file, base + 4, SEEK_SET) == 0
            && fread(count, 1, 2, file) == 2) {
            unsigned tables = ((unsigned)count[0] << 8) | count[1];
            if (fseek(file, base + 12, SEEK_SET) == 0) {
                ok = true;
                for (unsigned i = 0; i < tables && ok; ++i) {
                    unsigned char record[16];
                    size_t got = fread(record, 1, sizeof record, file);
                    if (got != sizeof record) {
                        ok = false;
                        break;
                    }
                    if (memcmp(record, "CFF2", 4) == 0) ok = false;
                }
            }
        }
    }
    fclose(file);
    return ok;
}

static bool fontconfig_lookup(char *path, size_t capacity, const char *family,
    const char *language, unsigned int probe, bool bold) {
    FcPattern *pattern = FcNameParse((const FcChar8 *)family);
    if (!pattern) return false;
    if (language)
        FcPatternAddString(pattern, FC_LANG, (const FcChar8 *)language);
    if (bold)
        FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    FcFontSet *set = FcFontSort(NULL, pattern, FcFalse, NULL, &result);
    FcPatternDestroy(pattern);
    if (!set) return false;
    bool found = false;
    for (int i = 0; i < set->nfont && !found; ++i) {
        FcPattern *font = set->fonts[i];
        FcCharSet *charset = NULL;
        FcChar8 *file = NULL;
        if (probe && (FcPatternGetCharSet(font, FC_CHARSET, 0, &charset)
                != FcResultMatch || !charset
                || !FcCharSetHasChar(charset, probe)))
            continue;
        int index = 0;
        if (FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch
            || !file
            || FcPatternGetInteger(font, FC_INDEX, 0, &index) != FcResultMatch
            || index != 0)
            continue;
        if (!rasterizable((const char *)file)) continue;
        snprintf(path, capacity, "%s", (const char *)file);
        found = true;
    }
    FcFontSetDestroy(set);
    return found;
}

static bool fontconfig_family(char *path, size_t capacity,
    const char *const *families, size_t count, const char *language,
    unsigned int probe, bool bold) {
    for (size_t i = 0; i < count; ++i)
        if (fontconfig_lookup(path, capacity, families[i], language, probe,
                bold))
            return true;
    return false;
}

const char *bongo_cat_ui_system_font(char *path, size_t capacity, bool multilingual) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        /* Windows 7 ships YaHei as TTF; newer Windows uses TTC. Probe
           both filenames instead of assuming the installed font format. */
        const char *multi[] = {"Fonts/msyh.ttc", "Fonts/msyh.ttf",
            "Fonts/msyhl.ttc", "Fonts/simsun.ttc"};
        const char *latin[] = {"Fonts/segoeui.ttf", "Fonts/msyhl.ttc"};
        const char **candidates = multilingual ? multi : latin;
        size_t count = multilingual ?
            sizeof(multi) / sizeof(multi[0]) : sizeof(latin) / sizeof(latin[0]);
        for (size_t i = 0; i < count; ++i) {
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
    static const char *const families[] = {"Noto Sans CJK SC",
        "Noto Sans CJK TC", "Noto Sans CJK JP", "Source Han Sans SC",
        "WenQuanYi Micro Hei", "sans-serif"};
    static const char *const latin_families[] = {"DejaVu Sans",
        "Noto Sans", "Liberation Sans", "FreeSans", "sans-serif"};
    const char *const *list = multilingual ? families : latin_families;
    size_t list_count = multilingual ?
        sizeof(families) / sizeof(families[0]) :
        sizeof(latin_families) / sizeof(latin_families[0]);
    const char *language = multilingual ? "zh-cn" : NULL;
    if (fontconfig_family(path, capacity, list, list_count, language,
            multilingual ? 0x4e2d : 'A', false))
        return path;
#endif
    return NULL;
}

const char *bongo_cat_ui_system_heading_font(char *path, size_t capacity,
    bool multilingual) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        /* Prefer bold in either format, then a regular Chinese face. */
        const char *multi[] = {"Fonts/msyhbd.ttc", "Fonts/msyhbd.ttf",
            "Fonts/msyh.ttc", "Fonts/msyh.ttf", "Fonts/msyhl.ttc",
            "Fonts/simsun.ttc"};
        const char *latin[] = {"Fonts/seguisb.ttf", "Fonts/segoeui.ttf"};
        const char **candidates = multilingual ? multi : latin;
        size_t count = multilingual ?
            sizeof(multi) / sizeof(multi[0]) : sizeof(latin) / sizeof(latin[0]);
        for (size_t i = 0; i < count; ++i) {
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
    static const char *const families[] = {"Noto Sans CJK SC",
        "Noto Sans CJK TC", "Noto Sans CJK JP", "Source Han Sans SC",
        "WenQuanYi Micro Hei", "sans-serif"};
    static const char *const latin_families[] = {"DejaVu Sans",
        "Noto Sans", "Liberation Sans", "FreeSans", "sans-serif"};
    const char *const *list = multilingual ? families : latin_families;
    size_t list_count = multilingual ?
        sizeof(families) / sizeof(families[0]) :
        sizeof(latin_families) / sizeof(latin_families[0]);
    const char *language = multilingual ? "zh-cn" : NULL;
    if (fontconfig_family(path, capacity, list, list_count, language,
            multilingual ? 0x4e2d : 'A', true))
        return path;
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
    static const char *const families[] = {"Noto Sans CJK KR",
        "Noto Sans KR", "NanumGothic", "sans-serif"};
    if (fontconfig_family(path, capacity, families,
            sizeof(families) / sizeof(families[0]), "ko-kr", 0xac00, false))
        return path;
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
    static const char *const families[] = {"Noto Sans CJK KR",
        "Noto Sans KR", "NanumGothic", "sans-serif"};
    if (fontconfig_family(path, capacity, families,
            sizeof(families) / sizeof(families[0]), "ko-kr", 0xac00, true))
        return path;
#endif
    return bongo_cat_ui_system_heading_font(path, capacity, true);
}
