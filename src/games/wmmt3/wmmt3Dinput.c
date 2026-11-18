/* WMMT3 STRPCB effects. Deliberately separate from the ID4/5 adapter. */
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <string.h>
#include <stdlib.h>
#include "wmmt3Dinput.h"
#include "../../input/idas_di.h"
#include "../../config/config.h"
#include "wmmt3Config.h"
#include "../../log/log.h"
#include "../../devices/strpcb.h"

static IDirectInputEffect *spring_fx, *damper_fx, *reflect_fx;
static bool autocenter_saved;
static DWORD old_autocenter;

static LONG gain(int percent)
{
    return percent < 0 ? 0 : percent > 100 ? 10000 : percent * 100;
}

static LONG coefficient(unsigned int value, int percent)
{
    if (value > 127) value = 127;
    return (LONG)(value * gain(percent) / 127);
}

static LONG center_offset(const uint8_t frame[10])
{
    unsigned int center = frame[2] | ((frame[3] & 3) << 8);
    /* Same 0..1023 position convention as wmmt3_input_wheel_position(). */
    return (LONG)(center * 20000 / 1023) - 10000;
}

static void release_effect(IDirectInputEffect **effect)
{
    if (*effect) {
        IDirectInputEffect_Stop(*effect);
        IDirectInputEffect_Release(*effect);
        *effect = NULL;
    }
}

static bool update_effect(IDirectInputDevice8A *dev, IDirectInputEffect **effect,
                          REFGUID guid, void *params, DWORD size)
{
    DWORD axis = DIJOFS_X;
    LONG direction = DI_FFNOMINALMAX;
    DIEFFECT fx;
    memset(&fx, 0, sizeof(fx));
    fx.dwSize = sizeof(fx);
    fx.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    fx.dwDuration = INFINITE;
    fx.dwGain = DI_FFNOMINALMAX;
    fx.dwTriggerButton = DIEB_NOTRIGGER;
    fx.cAxes = 1;
    fx.rgdwAxes = &axis;
    fx.rglDirection = &direction;
    fx.cbTypeSpecificParams = size;
    fx.lpvTypeSpecificParams = params;

    HRESULT hr;
    if (*effect) {
        hr = IDirectInputEffect_SetParameters(*effect, &fx, DIEP_TYPESPECIFICPARAMS);
        if (SUCCEEDED(hr)) {
            DWORD status = 0;
            hr = IDirectInputEffect_GetEffectStatus(*effect, &status);
            if (SUCCEEDED(hr) && (status & DIEGES_PLAYING)) return true;
            hr = IDirectInputEffect_Start(*effect, 1, 0);
            if (SUCCEEDED(hr)) return true;
        }
        release_effect(effect);
    }
    hr = IDirectInputDevice8_CreateEffect(dev, guid, &fx, effect, NULL);
    if (SUCCEEDED(hr)) hr = IDirectInputEffect_Start(*effect, 1, 0);
    if (FAILED(hr)) {
        release_effect(effect);
        log_debug(plfWmmt3, "STRPCB DirectInput effect failed: %08x", (unsigned int)hr);
        return false;
    }
    return true;
}

void wmmt3_di_ffb_apply(const uint8_t frame[10], bool running)
{
    IDirectInputDevice8A *dev = idas_di_ffb_device();
    if (!dev) return;
    if (!running) {
        release_effect(&spring_fx);
        release_effect(&damper_fx);
        release_effect(&reflect_fx);
        return;
    }

    /* Disable the driver's own spring only for this game, restoring on close. */
    if (!autocenter_saved) {
        DIPROPDWORD prop;
        memset(&prop, 0, sizeof(prop));
        prop.diph.dwSize = sizeof(prop);
        prop.diph.dwHeaderSize = sizeof(prop.diph);
        prop.diph.dwHow = DIPH_DEVICE;
        if (SUCCEEDED(IDirectInputDevice8_GetProperty(dev, DIPROP_AUTOCENTER, &prop.diph))) {
            old_autocenter = prop.dwData;
            prop.dwData = DIPROPAUTOCENTER_OFF;
            if (SUCCEEDED(IDirectInputDevice8_SetProperty(dev, DIPROP_AUTOCENTER, &prop.diph)))
                autocenter_saved = true;
        }
    }

    DICONDITION spring;
    memset(&spring, 0, sizeof(spring));
    spring.lOffset = center_offset(frame);
    spring.lPositiveCoefficient = spring.lNegativeCoefficient = coefficient(frame[4], Wmmt3Config.ffb_damper);
    spring.dwPositiveSaturation = spring.dwNegativeSaturation = DI_FFNOMINALMAX;
    int dead_band = Wmmt3Config.ffb_dead_band;
    if (dead_band < 0) dead_band = 0;
    if (dead_band > 1000) dead_band = 1000;
    spring.lDeadBand = dead_band * 10; /* retain the existing per-mille convention */
    if (spring.lPositiveCoefficient)
        update_effect(dev, &spring_fx, &GUID_Spring, &spring, sizeof(spring));
    else release_effect(&spring_fx);

    DICONDITION damper;
    memset(&damper, 0, sizeof(damper));
    damper.lPositiveCoefficient = damper.lNegativeCoefficient = coefficient(frame[6], Wmmt3Config.ffb_damper);
    damper.dwPositiveSaturation = damper.dwNegativeSaturation = DI_FFNOMINALMAX;
    /* Do not introduce resistance when the game requests zero viscosity. */
    if (damper.lPositiveCoefficient)
        update_effect(dev, &damper_fx, &GUID_Damper, &damper, sizeof(damper));
    else release_effect(&damper_fx);

    int reflect = (int8_t)frame[8];
    if (Wmmt3Config.ffb_invert) reflect = -reflect;
    if (reflect < -127) reflect = -127;
    if (reflect > 127) reflect = 127;
    DICONSTANTFORCE force = {reflect * gain(Wmmt3Config.ffb_constant_force) / 127};
    if (force.lMagnitude)
        update_effect(dev, &reflect_fx, &GUID_ConstantForce, &force, sizeof(force));
    else release_effect(&reflect_fx);
}

void wmmt3_di_ffb_close(void)
{
    wmmt3_di_ffb_apply(NULL, false);
    IDirectInputDevice8A *dev = idas_di_ffb_device();
    if (dev && autocenter_saved) {
        DIPROPDWORD prop;
        memset(&prop, 0, sizeof(prop));
        prop.diph.dwSize = sizeof(prop);
        prop.diph.dwHeaderSize = sizeof(prop.diph);
        prop.diph.dwHow = DIPH_DEVICE;
        prop.dwData = old_autocenter;
        IDirectInputDevice8_SetProperty(dev, DIPROP_AUTOCENTER, &prop.diph);
        autocenter_saved = false;
    }
}

static CRITICAL_SECTION state_lock;
static HANDLE wake_event, worker_thread;
static volatile LONG ready, closing;
static bool stopping, motor_running, have_frame;
static uint8_t latest_frame[10];

static DWORD WINAPI worker(void *unused)
{
    (void)unused;
    for (;;) {
        /* Retry unchanged effects as well: a lost device may recover without
         * the game changing its requested force. Silence is not motor-off. */
        WaitForSingleObject(wake_event, 100);
        uint8_t frame[10];
        EnterCriticalSection(&state_lock);
        bool stop = stopping;
        bool running = motor_running && have_frame;
        memcpy(frame, latest_frame, sizeof(frame));
        LeaveCriticalSection(&state_lock);
        if (stop) break;
        wmmt3_di_ffb_apply(frame, running);
    }
    wmmt3_di_ffb_close();
    return 0;
}

static void shutdown_worker(void)
{
    if (!InterlockedCompareExchange(&ready, 0, 0)) return;
    if (!InterlockedExchange(&closing, 1)) {
        EnterCriticalSection(&state_lock);
        stopping = true;
        SetEvent(wake_event);
        LeaveCriticalSection(&state_lock);
    }
    /* Every shutdown caller waits, so the shared device's console handler
     * cannot reset/unacquire it while our worker is still submitting effects. */
    WaitForSingleObject(worker_thread, INFINITE);
    /* Keep handles/lock alive until process exit for concurrent callbacks. */
}

static BOOL WINAPI console_ctrl(DWORD type)
{
    (void)type;
    shutdown_worker();
    return FALSE;
}

static void on_frame(const uint8_t frame[10])
{
    if (!InterlockedCompareExchange(&ready, 0, 0)) return;
    EnterCriticalSection(&state_lock);
    if (!stopping) {
        memcpy(latest_frame, frame, sizeof(latest_frame));
        have_frame = true;
        SetEvent(wake_event);
    }
    LeaveCriticalSection(&state_lock);
}

void wmmt3_di_ffb_motor(bool running)
{
    if (!InterlockedCompareExchange(&ready, 0, 0)) return;
    EnterCriticalSection(&state_lock);
    if (!stopping) {
        motor_running = running;
        /* Require a fresh frame after power returns. */
        if (!running) have_frame = false;
        SetEvent(wake_event);
    }
    LeaveCriticalSection(&state_lock);
}

bool wmmt3_di_ffb_init(void)
{
    if (InterlockedCompareExchange(&ready, 0, 0)) return !closing;
    if (!Wmmt3Config.ffb_enable || !idas_di_ffb_device()) return false;
    InitializeCriticalSection(&state_lock);
    wake_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!wake_event) {
        DeleteCriticalSection(&state_lock);
        return false;
    }
    worker_thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!worker_thread) {
        CloseHandle(wake_event);
        DeleteCriticalSection(&state_lock);
        return false;
    }
    InterlockedExchange(&ready, 1);
    atexit(shutdown_worker);
    SetConsoleCtrlHandler(console_ctrl, TRUE);
    strpcb_set_frame_sink(on_frame);
    return true;
}
