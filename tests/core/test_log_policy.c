#include "bongo_cat/log.h"

#include <stdio.h>

int main(void) {
    int failures = 0;
    const int categories[] = {SDL_LOG_CATEGORY_APPLICATION,
        SDL_LOG_CATEGORY_INPUT, SDL_LOG_CATEGORY_VIDEO,
        BONGO_CAT_LOG_LIFECYCLE, BONGO_CAT_LOG_UPDATE,
        BONGO_CAT_LOG_INPUT, BONGO_CAT_LOG_INPUT + 1};
    for (unsigned i = 0; i < sizeof(categories) / sizeof(categories[0]); ++i) {
        int category = categories[i];
        for (int value = SDL_LOG_PRIORITY_TRACE;
            value <= SDL_LOG_PRIORITY_CRITICAL; ++value) {
            SDL_LogPriority priority = (SDL_LogPriority)value;
            bool expected = priority >= SDL_LOG_PRIORITY_WARN ||
                (priority == SDL_LOG_PRIORITY_INFO &&
                    (category == BONGO_CAT_LOG_LIFECYCLE ||
                        category == BONGO_CAT_LOG_UPDATE ||
                        category == BONGO_CAT_LOG_INPUT));
            if (bongo_cat_log_enabled(category, priority) != expected) {
                fprintf(stderr, "Unexpected log policy: category=%d priority=%d\n",
                    category, value);
                ++failures;
            }
        }
    }
    return failures != 0;
}
