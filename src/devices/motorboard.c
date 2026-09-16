
#include <stdint.h>
#include <string.h>

#include "../config/config.h"
#include "../log/log.h"
#include "../hooks/file.h"
#include "motorboard.h"
#include "jvs.h"

static const struct ffb_ops *g_backend;

void ffb_init(const struct ffb_ops *ops)
{
    if (Config.ffb.enable)
    {
        g_backend = ops;
        log_info(plfFFB, "FFB initialized");
    }
}

void ffb_req_toggle(const uint8_t *bytes)
{
    uint8_t activate = bytes[2];
    if (activate == 0x01)
    {
        log_debug(plfFFB, "FFB: Activated\n");
    }
    else
    {
        log_debug(plfFFB, "FFB: Deactivated\n");
    }
    if (g_backend && g_backend->req_toggle)
        g_backend->req_toggle(activate == 0x01);
}

void ffb_req_constant_force(const uint8_t *bytes)
{
    uint8_t direction = bytes[1];
    uint8_t force = bytes[2];
    if (direction == 0x0)
    {
        // Right
        force = 128 - force;
    }
    if (g_backend && g_backend->req_constant_force)
        g_backend->req_constant_force(direction, force);
}

void ffb_req_rumble(const uint8_t *bytes)
{
    uint8_t force = bytes[1];
    uint8_t period = bytes[2];
    if (g_backend && g_backend->req_rumble)
        g_backend->req_rumble(force, period);
}

void ffb_req_damper(const uint8_t *bytes)
{
    uint8_t force = bytes[2];
    if (g_backend && g_backend->req_damper)
        g_backend->req_damper(force);
}


ssize_t motorboardWrite(int fd, const void *buf, size_t count)
{ 
    if (count >= 4)
        handleMotorPacket((uint8_t *)buf, 4);

    if (count == 7)
        handleMotorPacket((uint8_t *)buf + 4, 4);
    
    return count;
}

void handleMotorPacket(uint8_t *buf, int player)
{
    switch (buf[0])
    {
        case 0x80:
            ffb_req_toggle(buf);
        break;
        //case 0x83:
        case 0x85:
            ffb_req_rumble(buf);
            break;
        case 0x86:
            ffb_req_damper(buf);
            break;
        case 0x84:
            ffb_req_constant_force(buf);
            break;
        default:
            break;
    }
}