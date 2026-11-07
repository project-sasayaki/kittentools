#include <stddef.h>

#include "wmmt3.h"
#include "wmmt3Kickback.h"
#include "wmmt3Ffb.h"
#include "wmmt3Input.h"
#include "../../devices/strpcb.h"
#include "../../elfLoader/symbol.h"
#include "../../log/log.h"

typedef int (*KickbackCall)(void *self);

static KickbackCall next_onPower, next_offPower, next_waitOnPower, next_waitOffPower, next_requestSelfCheck;

static int hook_onPower(void *self)
{
    int result = next_onPower(self);
    strpcb_report_motor_power(true);
    wmmt3_ffb_motor(true);
    return result;
}

static int hook_offPower(void *self)
{
    int result = next_offPower(self);
    strpcb_report_motor_power(false);
    wmmt3_ffb_motor(false);
    return result;
}

/* Queued before the wait reads it. */
static int hook_waitOnPower(void *self)
{
    strpcb_report_motor_power(true);
    return next_waitOnPower(self);
}

static int hook_waitOffPower(void *self)
{
    strpcb_report_motor_power(false);
    return next_waitOffPower(self);
}

static int hook_requestSelfCheck(void *self)
{
    int result = next_requestSelfCheck(self);
    strpcb_report_self_check();
    return result;
}

static int hook(const char *name, void *replacement, void **next)
{
    if (!wmmt3_hook(name, replacement, next))
    {
        log_warn(plfWmmt3, "Could not hook %s; the steering check will not pass", name);
        return 0;
    }
    return 1;
}

void wmmt3_install_kickback_hooks(void)
{
    int installed = 0;
    installed += hook("_ZN10clKickback7onPowerEv", hook_onPower, (void **)&next_onPower);
    installed += hook("_ZN10clKickback8offPowerEv", hook_offPower, (void **)&next_offPower);
    installed += hook("_ZN10clKickback11waitOnPowerEv", hook_waitOnPower, (void **)&next_waitOnPower);
    installed += hook("_ZN10clKickback12waitOffPowerEv", hook_waitOffPower, (void **)&next_waitOffPower);
    installed += hook("_ZN10clKickback16requestSelfCheckEv", hook_requestSelfCheck, (void **)&next_requestSelfCheck);

    strpcb_set_position_source(wmmt3_input_wheel_position);

    void *instance = get_native_symbol_ptr("_ZN10clKickback11sm_instanceE");
    if (instance)
        strpcb_set_instance((void *const *)instance);
    else
        log_warn(plfWmmt3, "clKickback::sm_instance not found; STRPCB cannot see the self check bit");

    log_info(plfWmmt3, "STRPCB: %d clKickback hooks installed", installed);
}
