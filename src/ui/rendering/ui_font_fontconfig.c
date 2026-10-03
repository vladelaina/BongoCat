#include "ui_font.h"
#include "bongo_cat/file.h"

#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <string.h>

const char *bongo_cat_ui_fontconfig_font(char *path, size_t capacity,
    const char *language, bool bold) {
    if (!path || !capacity) return NULL;
    FcConfig *config = FcInitLoadConfigAndFonts();
    if (!config) return NULL;
    FcPattern *request = FcPatternCreate();
    if (!request) {
        FcConfigDestroy(config);
        return NULL;
    }
    if (!FcPatternAddString(request, FC_FAMILY, (const FcChar8 *)"sans-serif") ||
        !FcPatternAddString(request, FC_LANG, (const FcChar8 *)language) ||
        !FcPatternAddInteger(request, FC_WEIGHT,
            bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR) ||
        !FcPatternAddBool(request, FC_SCALABLE, FcTrue) ||
        !FcConfigSubstitute(config, request, FcMatchPattern)) {
        FcPatternDestroy(request);
        FcConfigDestroy(config);
        return NULL;
    }
    FcDefaultSubstitute(request);
    FcResult result;
    /* Do not trim: an earlier face may be unsupported by our rasterizer. */
    FcFontSet *fonts = FcFontSort(config, request, FcFalse, NULL, &result);
    bool found = false;
    FcChar32 probe = strcmp(language, "ko") == 0 ? 0xac00 :
        strcmp(language, "zh-cn") == 0 ? 0x4e2d : 'A';
    for (int i = 0; fonts && i < fonts->nfont; ++i) {
        FcPattern *font = fonts->fonts[i];
        FcChar8 *file = NULL, *format = NULL;
        FcCharSet *characters = NULL;
        int index = 0;
        if (FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch ||
            FcPatternGetString(font, FC_FONTFORMAT, 0, &format) != FcResultMatch ||
            FcPatternGetCharSet(font, FC_CHARSET, 0, &characters) != FcResultMatch)
            continue;
        /* Nuklear always bakes face zero and cannot select named instances.
           Reject bitmap/other formats that stb_truetype cannot rasterize. */
        if (FcPatternGetInteger(font, FC_INDEX, 0, &index) != FcResultMatch ||
            index != 0 || (strcmp((const char *)format, "TrueType") != 0 &&
            strcmp((const char *)format, "CFF") != 0) ||
            !FcCharSetHasChar(characters, probe) ||
            strlen((const char *)file) >= capacity) continue;
        FILE *stream = bongo_cat_file_open((const char *)file, "rb");
        if (!stream) continue;
        fclose(stream);
        snprintf(path, capacity, "%s", (const char *)file);
        found = true;
        break;
    }
    if (fonts) FcFontSetDestroy(fonts);
    FcPatternDestroy(request);
    FcConfigDestroy(config);
    return found ? path : NULL;
}
