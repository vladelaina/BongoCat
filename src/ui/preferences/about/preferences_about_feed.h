#ifndef BONGO_CAT_PREFERENCES_ABOUT_FEED_H
#define BONGO_CAT_PREFERENCES_ABOUT_FEED_H

#include <SDL3/SDL.h>

#define BONGO_ABOUT_CONTRIBUTOR_CAP 64
#define BONGO_ABOUT_AVATAR_SIZE 128
typedef struct BongoCatAboutContributor {
    char name[256];
    char profile[1024];
    unsigned char *pixels;
} BongoCatAboutContributor;
typedef struct BongoCatAboutFeed {
    int count;
    BongoCatAboutContributor people[BONGO_ABOUT_CONTRIBUTOR_CAP];
} BongoCatAboutFeed;
/* Parsing owns decoded avatar pixels until feed_free. */
BongoCatAboutFeed *bongo_cat_about_feed_parse(char *svg, SDL_AtomicInt *cancel);
void bongo_cat_about_feed_free(BongoCatAboutFeed *feed);

#endif
