#pragma once

#include <stdbool.h>
#include <stdint.h>

/* CRC as main.c computes it */
#define WMMT3_JPN_V337   0xcf5dde93
#define WMMT3_EXP        0xec016afd
#define WMMT3DX_EXP      0xe47ddd0d
#define WMMT3DX_EXP_C09  0x050db385
#define WMMT3DX_JPN      0xfa4b36bc
#define WMMT3DXP_JPN     0x85a0e7ab
#define WMMT3DXP_CHN     0x4753d367
#define WMMT3DXP_EXP     0x827c956b

typedef struct _WMMT3_InitialData {
    /* Market digit: 1 JPN, 2/3/4 EXP, 5 CHN. */
    const char *gameID;
    const char *verName;
    const char *windowTitle;
} WMMT3_InitialData;

bool wmmt3_hook(const char *name, void *replacement, void **next);

bool is_wmmt3_crc(uint32_t crc);

void init_wmmt3_prepatch(uint32_t crc);
void wmmt3_install_bink_events(void);
