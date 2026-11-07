/* Effects are sent from a worker thread: SetParameters is a USB round trip. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

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
static bool g_started;

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

    for (;;)
    {
        WaitForSingleObject(g_wake, INFINITE);

        EnterCriticalSection(&g_lock);
        int motor = g_wantMotor;
        int damper = g_wantDamper;
        int force = g_wantForce;
        int direction = g_wantDirection;
        g_wantMotor = -1;
        LeaveCriticalSection(&g_lock);

        if (motor >= 0 && backend->ffb_req_toggle)
        {
            backend->ffb_req_toggle(motor != 0);
            if (!motor)
                sentDamper = sentForce = sentDirection = -1;
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
    if (!Config.ffb.enable || !g_started)
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

void wmmt3_ffb_motor(bool running)
{
    if (!g_started)
        return;
    EnterCriticalSection(&g_lock);
    g_wantMotor = running ? 1 : 0;
    if (!running)
        g_wantDamper = g_wantForce = g_wantDirection = -1;
    LeaveCriticalSection(&g_lock);
    post();
}

void wmmt3_ffb_init(void)
{
    const struct idas_io_backend *backend = wmmt3_input_backend();
    if (backend && backend->ffb_init)
        backend->ffb_init();
    strpcb_set_frame_sink(on_frame);

    if (backend && Config.ffb.enable)
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
    log_info(plfWmmt3, "Force feedback: %s", g_started ? "on (worker thread)" : "off");
}
