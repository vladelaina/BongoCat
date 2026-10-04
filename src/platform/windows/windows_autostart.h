#ifndef BONGO_CAT_WINDOWS_AUTOSTART_H
#define BONGO_CAT_WINDOWS_AUTOSTART_H

#include "bongo_cat/common.h"

#ifdef __cplusplus
extern "C" {
#endif
bool bongo_cat_windows_autostart_command(int argc, char **argv, int *exit_code);
#ifdef __cplusplus
}
#endif
#endif
