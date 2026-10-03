#ifndef BONGO_CAT_RUNTIME_DIAGNOSTICS_H
#define BONGO_CAT_RUNTIME_DIAGNOSTICS_H

/* Phase names must be string literals. These calls do not write per-frame logs. */
#ifdef _WIN32
void bongo_cat_diagnostics_start(const char *state_root);
const char *bongo_cat_diagnostics_phase(const char *phase);
void bongo_cat_diagnostics_stop(void);
#else
static inline void bongo_cat_diagnostics_start(const char *root) { (void)root; }
static inline const char *bongo_cat_diagnostics_phase(const char *phase) {
    (void)phase; return 0;
}
static inline void bongo_cat_diagnostics_stop(void) {}
#endif
#endif
