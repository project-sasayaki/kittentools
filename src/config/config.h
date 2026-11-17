#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CONFIG_PATH "kitten/config.ini"
#define IDAS_CONFIG_PATH "idas.ini"

#define MAX_PATH_LENGTH 1024
#define INPUT_STRING_LENGTH 256

#define DIPSW_RES_1360x768  0b00'111'000
#define DIPSW_RES_1280x1024 0b00'110'000
#define DIPSW_RES_1024x600  0b00'101'000
#define DIPSW_RES_800x600   0b00'100'000
#define DIPSW_RES_1280x768  0b00'011'000
#define DIPSW_RES_1024x768  0b00'010'000
#define DIPSW_RES_800x480   0b00'001'000
#define DIPSW_RES_DEFAULT   0b00'000'000


typedef enum
{
    NVIDIA_GPU,
    AMD_GPU,
    ATI_GPU,
    INTEL_GPU,
    UNKNOWN_GPU,
    ERROR_GPU
} GpuType;

typedef enum
{
    INPUT_DINPUT,
    INPUT_XINPUT
} InputMode;

typedef enum
{
    SEGA_TYPE_1,
    SEGA_TYPE_3,
    NAMCO_FCA1
} JVSIOType;

/* The parts of config.def (its GROUP() lines). A game names the ones it uses in its GameModule and
 * kitten/config.ini holds those; CONFIG_GROUP_COMMON is held for every game. */
typedef enum
{
    CONFIG_GROUP_COMMON = 1 << 0,
    CONFIG_GROUP_DISPLAY = 1 << 1,
    CONFIG_GROUP_NETWORK = 1 << 2,
    CONFIG_GROUP_SYSCONF = 1 << 3,
    CONFIG_GROUP_COINS = 1 << 4,
    CONFIG_GROUP_PATH = 1 << 5,
    CONFIG_GROUP_IO3 = 1 << 6,
    CONFIG_GROUP_FFB = 1 << 7,
    CONFIG_GROUP_XINPUT = 1 << 8,
    CONFIG_GROUP_DINPUT = 1 << 9
} ConfigGroup;

typedef enum
{
    CONFIG_ITEM_SECTION,
    CONFIG_ITEM_COMMENT,
    CONFIG_ITEM_NEWLINE,
    CONFIG_ITEM_GROUP,
    CONFIG_ITEM_STR,
    CONFIG_ITEM_BOOL,
    CONFIG_ITEM_INT,
    CONFIG_ITEM_HEX,
    CONFIG_ITEM_IPV4,
    CONFIG_ITEM_RENAMED_INT,
    CONFIG_ITEM_RENAMED_STR
} ConfigItemType;

/* One line of a settings definition (.def): a setting, or what stands around it in the file. */
typedef struct ConfigItem
{
    ConfigItemType type;
    const char *section;
    const char *name;
    const char *comment;
    size_t offset;   /* of the setting's value in the table's values */
    int precision;   /* hex: digits written */
    unsigned group;  /* CONFIG_ITEM_GROUP: the group of the items that follow */
    const char *defaultStr;
    int defaultInt;
    unsigned int defaultIpv4;
} ConfigItem;

/* The settings of one definition and where their values are kept. */
typedef struct ConfigTable
{
    const ConfigItem *items;
    size_t count;
    void *values;
} ConfigTable;

typedef struct config {
#define SECTION(s, text) struct {
#define ENDSECTION(s) } s;
#include "configDefFields.h"
#include "config.def"
    bool _keep_linter_happy;
} config_t;

extern config_t Config;

/* Reads the settings of config.def from kitten/config.ini. */
void load_config();
/* Reads a game's own settings from kitten/config.ini. */
void config_read_table(const ConfigTable *table);
/* A game's table and which of its GROUP()s to write. What it holds outside any GROUP() is always written. */
typedef struct ConfigSelection
{
    const ConfigTable *table;
    unsigned groups;
} ConfigSelection;

/* Writes kitten/config.ini: these groups of config.def and the common one, as load_config() read them,
 * then these tables (one without a table is passed over). */
void config_write(unsigned groups, const ConfigSelection *own, size_t ownCount);
int getNetHostAddr();
int getNetHostMask();
int getNetSubnetAddr();
void setServerBox(bool flag);
bool isServerBox();