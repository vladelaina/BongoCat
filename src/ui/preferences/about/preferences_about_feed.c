#include "preferences_about_feed.h"
#include "preferences_about_svg.h"
#include <stb_image.h>
#include <webp/decode.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct FeedParser {
    BongoCatAboutFeed *feed;
    SDL_AtomicInt *cancel;
    bool in_link, in_title, failed;
    BongoCatAboutContributor person;
} FeedParser;

static int base64_digit(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}

static unsigned char *decode_uri(const char *uri, int *length) {
    const char *data = NULL;
    const char *prefixes[] = {"data:image/png;base64,", "data:image/jpeg;base64,",
        "data:image/webp;base64,"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
        if (!strncmp(uri, prefixes[i], strlen(prefixes[i]))) data = uri + strlen(prefixes[i]);
    if (!data) return NULL;
    size_t size = strlen(data);
    if (!size || size > 1024u * 1024u || size % 4) return NULL;
    unsigned char *bytes = malloc(size / 4 * 3);
    if (!bytes) return NULL;
    int used = 0;
    for (size_t i = 0; i < size; i += 4) {
        int a = base64_digit((unsigned char)data[i]);
        int b = base64_digit((unsigned char)data[i + 1]);
        int c = data[i + 2] == '=' ? 0 : base64_digit((unsigned char)data[i + 2]);
        int d = data[i + 3] == '=' ? 0 : base64_digit((unsigned char)data[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 ||
            ((data[i + 2] == '=' || data[i + 3] == '=') && i + 4 != size) ||
            (data[i + 2] == '=' && data[i + 3] != '=')) { free(bytes); return NULL; }
        bytes[used++] = (unsigned char)((a << 2) | (b >> 4));
        if (data[i + 2] != '=') bytes[used++] = (unsigned char)((b << 4) | (c >> 2));
        if (data[i + 3] != '=') bytes[used++] = (unsigned char)((c << 6) | d);
    }
    *length = used;
    return bytes;
}

static unsigned char *portrait(const char *uri, SDL_AtomicInt *cancel) {
    if (SDL_GetAtomicInt(cancel)) return NULL;
    int length = 0, width = 0, height = 0, channels = 0;
    unsigned char *data = decode_uri(uri, &length);
    if (!data) return NULL;
    bool webp = length >= 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WEBP", 4);
    bool known = webp ? WebPGetInfo(data, (size_t)length, &width, &height) != 0 :
        stbi_info_from_memory(data, length, &width, &height, &channels) != 0;
    if (!known || width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        (size_t)width * (size_t)height > 4u * 1024u * 1024u) { free(data); return NULL; }
    const int size = BONGO_ABOUT_AVATAR_SIZE;
    if (SDL_GetAtomicInt(cancel)) { free(data); return NULL; }
    /* Decode WebP directly at display resolution instead of allocating the
       full source image and then discarding almost all of its pixels. */
    WebPDecoderConfig config;
    unsigned char *source = NULL;
    if (webp) {
        if (!WebPInitDecoderConfig(&config)) { free(data); return NULL; }
        source = malloc((size_t)size * (size_t)size * 4);
        if (!source) { free(data); return NULL; }
        config.output.colorspace = MODE_RGBA;
        config.output.is_external_memory = 1;
        config.output.u.RGBA.rgba = source;
        config.output.u.RGBA.stride = size * 4;
        config.output.u.RGBA.size = (size_t)size * (size_t)size * 4;
        config.options.use_scaling = 1;
        config.options.scaled_width = size;
        config.options.scaled_height = size;
        if (WebPDecode(data, (size_t)length, &config) == VP8_STATUS_OK) {
            source = config.output.u.RGBA.rgba;
            width = height = size;
        } else {
            WebPFreeDecBuffer(&config.output);
            free(source);
            source = NULL;
        }
    } else {
        source = stbi_load_from_memory(data, length, &width, &height, &channels, 4);
    }
    free(data);
    if (!source) return NULL;
    if (SDL_GetAtomicInt(cancel)) {
        if (webp) { WebPFreeDecBuffer(&config.output); free(source); }
        else stbi_image_free(source);
        return NULL;
    }
    unsigned char *pixels = webp ? source : malloc((size_t)size * (size_t)size * 4);
    if (pixels) for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        unsigned char *out = pixels + (y * size + x) * 4;
        int sx = x * width / size, sy = y * height / size;
        if (!webp) memcpy(out, source + ((size_t)sy * (size_t)width + (size_t)sx) * 4, 4);
        int dx = 2 * x + 1 - size, dy = 2 * y + 1 - size;
        if (dx * dx + dy * dy > size * size) out[3] = 0;
    }
    if (webp) WebPFreeDecBuffer(&config.output); else stbi_image_free(source);
    return pixels;
}

static const char *attribute(const char **attrs, const char *name) {
    for (int i = 0; attrs[i] && attrs[i + 1]; i += 2)
        if (!strcmp(attrs[i], name)) return attrs[i + 1];
    return "";
}

static void xml_text(char *out, size_t capacity, const char *input) {
    size_t used = 0;
    while (*input && used + 1 < capacity) {
        const char *entities[] = {"&amp;", "&lt;", "&gt;", "&quot;", "&apos;"};
        const char values[] = {'&', '<', '>', '"', '\''};
        bool entity = false;
        for (size_t i = 0; i < 5; i++) if (!strncmp(input, entities[i], strlen(entities[i]))) {
            out[used++] = values[i]; input += strlen(entities[i]); entity = true; break;
        }
        if (!entity) out[used++] = *input++;
    }
    out[used] = 0;
}

static void start(void *user, const char *element, const char **attrs) {
    FeedParser *p = user;
    if (p->failed || SDL_GetAtomicInt(p->cancel)) return;
    if (!strcmp(element, "a")) {
        if (p->in_link) { p->failed = true; return; }
        p->in_link = true;
        memset(&p->person, 0, sizeof(p->person));
        const char *href = attribute(attrs, "href");
        if (!*href) href = attribute(attrs, "xlink:href");
        if (strlen(href) < sizeof(p->person.profile) && !strncmp(href, "https://", 8))
            xml_text(p->person.profile, sizeof(p->person.profile), href);
    } else if (p->in_link && !strcmp(element, "title")) p->in_title = true;
    else if (p->in_link && !strcmp(element, "image") && !p->person.pixels &&
             p->feed->count < BONGO_ABOUT_CONTRIBUTOR_CAP) {
        const char *href = attribute(attrs, "href");
        if (!*href) href = attribute(attrs, "xlink:href");
        p->person.pixels = portrait(href, p->cancel);
    }
}

static void end(void *user, const char *element) {
    FeedParser *p = user;
    if (!strcmp(element, "title")) p->in_title = false;
    if (!strcmp(element, "a")) {
        if (!p->failed && p->person.pixels && p->feed->count < BONGO_ABOUT_CONTRIBUTOR_CAP) {
            if (!p->person.name[0]) snprintf(p->person.name, sizeof(p->person.name), "Contributor");
            p->feed->people[p->feed->count++] = p->person;
            p->person.pixels = NULL;
        }
        free(p->person.pixels); p->person.pixels = NULL;
        p->in_link = p->in_title = false;
    }
}

static void content(void *user, const char *text) {
    FeedParser *p = user;
    if (p->in_link && p->in_title) xml_text(p->person.name, sizeof(p->person.name), text);
}

BongoCatAboutFeed *bongo_cat_about_feed_parse(char *svg, SDL_AtomicInt *cancel) {
    if (!svg || !strstr(svg, "<svg") || strstr(svg, "<!DOCTYPE") || strstr(svg, "<!ENTITY")) return NULL;
    BongoCatAboutFeed *feed = calloc(1, sizeof(*feed));
    if (!feed) return NULL;
    FeedParser parser = {0}; parser.feed = feed; parser.cancel = cancel;
    bongo_cat_about_svg_parse_xml(svg, start, end, content, &parser);
    free(parser.person.pixels);
    if (parser.failed || parser.in_link || SDL_GetAtomicInt(cancel)) {
        bongo_cat_about_feed_free(feed); return NULL;
    }
    return feed;
}

void bongo_cat_about_feed_free(BongoCatAboutFeed *feed) {
    if (!feed) return;
    for (int i = 0; i < feed->count; i++) free(feed->people[i].pixels);
    free(feed);
}
