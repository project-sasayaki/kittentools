#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "termios.h"
#include "collections.h"
#include "symbol.h"
#include "libc.h"
#include "../log/log.h"
#include "elfLoader.h"
#include <string.h>

static IntMap g_termios_state;

int hook_tcgetattr(int fd, struct linux_termios *termios_p);
int hook_tcsetattr(int fd, int optional_actions, const struct linux_termios *termios_p);
int hook_tcflush(int fd, int queue_selector);
int hook_tcdrain(int fd);
int hook_cfssetispeed(struct linux_termios *termios_p, unsigned int speed);
int hook_cfssetospeed(struct linux_termios *termios_p, unsigned int speed);


void termiosBridge_initBridges(void)
{
    log_debug(plfBridges, "Initializing Termios Bridges...");

    intMap_Init(&g_termios_state);

    symbol_register("tcgetattr", hook_tcgetattr, NULL);
    symbol_register("tcsetattr", hook_tcsetattr, NULL);
    symbol_register("tcflush", hook_tcflush, NULL);
    symbol_register("tcdrain", hook_tcdrain, NULL);
    symbol_register("cfgetispeed", hook_ret_zero, NULL);
    symbol_register("cfgetospeed", hook_ret_zero, NULL);
    symbol_register("cfsetispeed", hook_cfssetispeed, NULL);
    symbol_register("cfsetospeed", hook_cfssetospeed, NULL);
    symbol_register("cfsetspeed", hook_cfssetospeed, NULL);
    symbol_register("cfmakeraw", hook_ret_zero, NULL);
    symbol_register("isatty", hook_ret_zero, NULL);
}

int hook_tcgetattr(int fd, struct linux_termios *termios_p)
{
    if (!termios_p)
        return -1;

    struct linux_termios *existing = (struct linux_termios *)intMap_Get(&g_termios_state, fd);
    if (existing)
    {
        *termios_p = *existing;
    }
    else
    {
        memset(termios_p, 0, sizeof(struct linux_termios));
        termios_p->c_cflag = 0x1000 | 0x0800 | 0x0030; 
        termios_p->c_ispeed = 115200;
        termios_p->c_ospeed = 115200;
    }
    return 0;
}

int hook_tcsetattr(int fd, int optional_actions, const struct linux_termios *termios_p)
{
    if (!termios_p)
        return -1;

    struct linux_termios *existing = (struct linux_termios *)intMap_Get(&g_termios_state, fd);
    if (!existing)
    {
        existing = (struct linux_termios *)calloc(1, sizeof(struct linux_termios));
        intMap_Set(&g_termios_state, fd, existing);
    }
    *existing = *termios_p;

    log_debug(plfBridges, "tcsetattr: fd=%d, speed=%d, flags=0x%x", fd, termios_p->c_ospeed, termios_p->c_cflag);
    return 0;
}

int hook_tcflush(int fd, int queue_selector)
{
    return 0;
}

int hook_tcdrain(int fd)
{
    Sleep(1);
    return 0;
}

int hook_cfssetispeed(struct linux_termios *termios_p, unsigned int speed)
{
    if (termios_p)
        termios_p->c_ispeed = speed;
    return 0;
}

int hook_cfssetospeed(struct linux_termios *termios_p, unsigned int speed)
{
    if (termios_p)
        termios_p->c_ospeed = speed;
    return 0;
}
