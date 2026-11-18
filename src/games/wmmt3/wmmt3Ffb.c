/* Effects are sent from a worker thread: SetParameters is a USB round trip. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "wmmt3.h"
#include "wmmt3Ffb.h"
#include "wmmt3Input.h"
#include "../../config/config.h"
#include "../../devices/strpcb.h"
#include "../../input/backend.h"
#include "../../log/log.h"

/* The board's fields reach about 63; the effects take 0..127. */
static uint8_t scale(int value)
{
    value = abs(value) * 2;
    return (uint8_t)(value > 127 ? 127 : value);
}

static CRITICAL_SECTION g_lock;
static HANDLE g_wake;
static int g_wantDamper = -1;
static int g_wantForce = -1;
static int g_wantDirection = -1;
static int g_wantMotor = -1;        /* -1 none pending, 0 off, 1 on */
static bool g_wantPulse;
static bool g_motorRunning;
static bool g_started;
static bool g_gamepad;

static void post(void)
{
    if (g_wake)
        SetEvent(g_wake);
}

static DWORD WINAPI ffb_worker(void *unused)
{
    (void)unused;
    const struct idas_io_backend *backend = wmmt3_input_backend();
    int sentDamper = -1, sentForce = -1, sentDirection = -1;
    bool pulsing = false;
    ULONGLONG pulseEnd = 0;

    for (;;)
    {
        DWORD timeout = INFINITE;
        if (pulsing)
        {
            ULONGLONG now = GetTickCount64();
            timeout = now < pulseEnd ? (DWORD)(pulseEnd - now) : 0;
        }
        WaitForSingleObject(g_wake, timeout);

        EnterCriticalSection(&g_lock);
        int motor = g_wantMotor;
        int damper = g_wantDamper;
        int force = g_wantForce;
        int direction = g_wantDirection;
        bool pulse = g_wantPulse;
        g_wantMotor = -1;
        g_wantPulse = false;
        LeaveCriticalSection(&g_lock);

        if (motor >= 0 && backend->ffb_req_toggle)
        {
            backend->ffb_req_toggle(motor != 0);
            if (!motor)
            {
                sentDamper = sentForce = sentDirection = -1;
                pulsing = false;
            }
        }

        if (g_gamepad)
        {
            if (pulse && backend->ffb_req_rumble)
            {
                if (!pulsing)
                    backend->ffb_req_rumble(0, 127);
                pulsing = true;
                pulseEnd = GetTickCount64() + (ULONGLONG)Config.wmmt3.xinput_rumble_ms;
            }
            else if (pulsing && GetTickCount64() >= pulseEnd && backend->ffb_req_rumble)
            {
                backend->ffb_req_rumble(0, 0);
                pulsing = false;
            }
            continue;
        }

        if (damper >= 0 && damper != sentDamper && backend->ffb_req_damper)
        {
            backend->ffb_req_damper((uint8_t)damper);
            sentDamper = damper;
        }
        if (force >= 0 && (force != sentForce || direction != sentDirection) &&
            backend->ffb_req_constant_force)
        {
            backend->ffb_req_constant_force((uint8_t)direction, (uint8_t)force);
            sentForce = force;
            sentDirection = direction;
        }
    }
    return 0;
}

static void on_frame(const uint8_t frame[10])
{
    if (!g_started || g_gamepad)
        return;

    int spring = frame[4];
    int viscosity = frame[6];
    int reflect = (int8_t)frame[8];

    if (Config.wmmt3.ffb_invert)
        reflect = -reflect;

    int damper = scale(spring > viscosity ? spring : viscosity);
    int direction = reflect < 0 ? 0 : 1; /* 0 pushes right, 1 left */
    int force = scale(reflect);

    EnterCriticalSection(&g_lock);
    bool changed = damper != g_wantDamper || force != g_wantForce || direction != g_wantDirection;
    g_wantDamper = damper;
    g_wantForce = force;
    g_wantDirection = direction;
    LeaveCriticalSection(&g_lock);
    if (changed)
        post();
}

/* Called once a frame from one thread only, so g_onGap needs no lock. */
typedef bool (*CheckGapFn)(const void *self, float from, float to);
static CheckGapFn next_CheckGap;
static bool g_onGap;

static bool hook_CheckGap(const void *self, float from, float to)
{
    bool onGap = next_CheckGap(self, from, to);
    if (onGap && !g_onGap && g_started)
    {
        EnterCriticalSection(&g_lock);
        bool running = g_motorRunning;
        if (running)
            g_wantPulse = true;
        LeaveCriticalSection(&g_lock);
        if (running)
            post();
    }
    g_onGap = onGap;
    return onGap;
}

void wmmt3_ffb_motor(bool running)
{
    if (!g_started)
        return;
    EnterCriticalSection(&g_lock);
    g_motorRunning = running;
    g_wantMotor = running ? 1 : 0;
    if (!running)
    {
        g_wantDamper = g_wantForce = g_wantDirection = -1;
        g_wantPulse = false;
    }
    LeaveCriticalSection(&g_lock);
    post();
}

void wmmt3_ffb_init(void)
{
    const struct idas_io_backend *backend = wmmt3_input_backend();
    g_gamepad = Config.wmmt3.input_mode == INPUT_XINPUT;
    if (backend && backend->ffb_init)
        backend->ffb_init();
    strpcb_set_frame_sink(on_frame);

    bool wanted = g_gamepad ? Config.wmmt3.xinput_rumble > 0 && Config.wmmt3.xinput_rumble_ms > 0
                            : Config.wmmt3.ffb_enable;
    if (g_gamepad && wanted &&
        !wmmt3_hook("_ZNK2yt9SAreaData8CheckGapEff", (void *)hook_CheckGap, (void **)&next_CheckGap))
    {
        log_warn(plfWmmt3, "Gamepad rumble: yt::SAreaData::CheckGap not found; road joints will not rumble");
        wanted = false;
    }

    if (backend && wanted)
    {
        InitializeCriticalSection(&g_lock);
        g_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
        HANDLE thread = g_wake ? CreateThread(NULL, 0, ffb_worker, NULL, 0, NULL) : NULL;
        if (thread)
        {
            SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
            CloseHandle(thread);
            g_started = true;
        }
    }
    if (g_gamepad)
        log_info(plfWmmt3, "Gamepad rumble: %s (road joints, %d%%, %d ms)",
                 g_started ? "on" : "off", Config.wmmt3.xinput_rumble, Config.wmmt3.xinput_rumble_ms);
    else
        log_info(plfWmmt3, "Force feedback: %s", g_started ? "on (worker thread)" : "off");
}
