#pragma once

#include <stdint.h>
#include <stdbool.h>

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
    SEGA_TYPE_3
} JVSIOType;

typedef struct config {
#define SECTION(s, comment) struct {
#define CFG_str(s, n, default, comment) char* n;
#define CFG_bool(s, n, default, comment) bool n;
#define CFG_int(s, n, default, comment) int n;
#define CFG_hex(s, n, precision, default, comment) int n;
#define CFG_ipv4(s, n, a, b, c, d, comment) unsigned int n;
#define ENDSECTION(s) } s;
#include "config.def"
    bool _keep_linter_happy;
} config_t;

extern config_t Config;

void save_current_config(bool writeDefault);
void load_config();
int getNetHostAddr();
int getNetHostMask();
int getNetSubnetAddr();
void setServerBox(bool flag);
bool isServerBox();