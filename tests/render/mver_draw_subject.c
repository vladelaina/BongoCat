#include "mver_probe.h"
#define glDisable mver_probe_disable
#define glEnable mver_probe_enable
#define glBindTexture mver_probe_bind_texture
#define glDrawArrays mver_probe_draw_arrays
#include "../../src/render/mver_pointer_overlay_draw.c"
