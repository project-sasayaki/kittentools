#include <stdio.h>

#include "../config/config.h"
#include "securityBoard.h"
#include "../log/log.h"

#define SECURITY_BOARD_FRONT_PANEL 0x38
#define SECURITY_BOARD_FRONT_PANEL_NON_ROOT 0x1038
#define SECURITY_BOARD_KEYCHIP 0xFF

static const uint32_t dipMap[8] = {
    0x40,  /* DIP1 -> bit6  */
    0x80,  /* DIP2 -> bit7  */
    0x100, /* DIP3 -> bit8  */
    0x200, /* DIP4 -> bit9  */
    0x400, /* DIP5 -> bit10 */
    0x800, /* DIP6 -> bit11 */
    0x10,  /* DIP7 -> bit4  */
    0x20   /* DIP8 -> bit5  */
};

typedef struct
{
    int serviceSwitch;
    int testSwitch;
    int led[2];
} SecurityBoard;

SecurityBoard securityBoard = {0};

int securityBoardIn(uint16_t port, uint32_t *data)
{
    switch (port)
    {
        case SECURITY_BOARD_FRONT_PANEL_NON_ROOT:
        case SECURITY_BOARD_FRONT_PANEL:
        {
            uint32_t result = 0xFFFFFFFF;
            
            for (int i = 1; i <= 8; i++)
            {   
                if (Config.sysconf.dipsw & (1U << i))
                {
                    result &= ~dipMap[i - 1];
                }
            }

            if (securityBoard.serviceSwitch) 
                result &= ~0x08;
            if (securityBoard.testSwitch) 
                result &= ~0x04;
            *data = result;
        }
        break;

        default:
            break;
    }

    return 0;
}
