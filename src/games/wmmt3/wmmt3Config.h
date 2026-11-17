#pragma once

#include <stdbool.h>

#include "../../config/config.h"

/* The GROUP()s of wmmt3Config.def: the settings that only some versions have. A version names its
 * own in g_WMMT3Versions (wmmt3.c). */
typedef enum
{
    WMMT3_CONFIG_DX_PLUS = 1 << 0 /* 3DX+ */
} Wmmt3ConfigGroup;

/* The [wmmt3] section of kitten/config.ini (wmmt3Config.def), read once the game is recognised. */
typedef struct Wmmt3Settings
{
#include "../../config/configDefFields.h"
#include "wmmt3Config.def"
} Wmmt3Settings;

extern Wmmt3Settings Wmmt3Config;
extern const ConfigTable Wmmt3ConfigTable;
