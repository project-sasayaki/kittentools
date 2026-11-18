#include <stddef.h>

#include "wmmt3Config.h"

Wmmt3Settings Wmmt3Config = {
#include "../../config/configDefDefaults.h"
#include "wmmt3Config.def"
};

static const ConfigItem g_items[] = {
#define CONFIG_DEF_OFFSET(s, n) offsetof(Wmmt3Settings, n)
#include "../../config/configDefItems.h"
#include "wmmt3Config.def"
#undef CONFIG_DEF_OFFSET
};

const ConfigTable Wmmt3ConfigTable = {
    .items = g_items,
    .count = sizeof(g_items) / sizeof(g_items[0]),
    .values = &Wmmt3Config,
};
