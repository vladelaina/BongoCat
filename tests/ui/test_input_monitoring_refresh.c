#include "preferences_state.h"

#include <stdio.h>
#include <stdlib.h>

/* The target compiles the real preferences_pages.c with this getter substituted
   for the platform one, so the refresh path runs without touching TCC. */
static bool probe_authorized;
static unsigned probe_reads;

bool bongo_cat_test_input_monitoring_authorized(void) {
    probe_reads++;
    return probe_authorized;
}

static int failures;

static void check(bool condition, const char *what) {
    if (!condition) {
        fprintf(stderr, "failed: %s\n", what);
        failures++;
    }
}

int main(void) {
    BongoCatPreferences *value = calloc(1, sizeof(*value));
    if (!value) {
        fprintf(stderr, "cannot allocate preferences\n");
        return 2;
    }

    /* First reading: the page has never queried the platform, so the frame it is
       drawing now needs another one to show what was read. */
    probe_authorized = false;
    probe_reads = 0;
    value->render_dirty = false;
    bongo_cat_preferences_input_monitoring_refresh(value);
    check(value->input_monitoring_valid, "first refresh marks the reading valid");
    check(!value->input_monitoring_authorized,
        "first refresh stores the missing permission");
    check(value->render_dirty, "first refresh repaints the page");
    check(probe_reads == 1, "first refresh reads the platform once");

    /* The user granted the permission while the page was open: the button the
       current frame drew is stale, so the next frame has to draw again. */
    probe_authorized = true;
    value->render_dirty = false;
    bongo_cat_preferences_input_monitoring_refresh(value);
    check(value->input_monitoring_authorized,
        "granted refresh stores the permission");
    check(value->render_dirty, "granted refresh repaints the page");

    /* Revoked again: the same kind of change, in the other direction. */
    probe_authorized = false;
    value->render_dirty = false;
    bongo_cat_preferences_input_monitoring_refresh(value);
    check(!value->input_monitoring_authorized,
        "revoked refresh stores the missing permission");
    check(value->render_dirty, "revoked refresh repaints the page");

    /* An unchanged reading must not schedule a repaint, or every refresh point
       would keep the page rendering. */
    probe_reads = 0;
    value->render_dirty = false;
    bongo_cat_preferences_input_monitoring_refresh(value);
    check(!value->input_monitoring_authorized,
        "unchanged refresh keeps the reading");
    check(!value->render_dirty, "unchanged refresh does not repaint");
    check(probe_reads == 1, "unchanged refresh still reads the platform once");

    free(value);
    if (failures) return 1;
    puts("input monitoring refresh repaint checks passed");
    return 0;
}
