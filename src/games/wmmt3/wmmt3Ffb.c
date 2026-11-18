/* Effects are sent from a worker thread: SetParameters is a USB round trip. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#include "wmmt3.h"
#include "wmmt3Ffb.h"
#include "wmmt3Input.h"
#include "wmmt3Dinput.h"
#include "../../config/config.h"
#include "wmmt3Config.h"
#include "../../input/backend.h"
#include "../../log/log.h"

static CRITICAL_SECTION g_lock;
static HANDLE g_wake;
static int g_wantMotor = -1;        /* -1 none pending, 0 off, 1 on */
static bool g_wantPulse;
static bool g_motorRunning;
static bool g_started;

static void post(void)
{
    if (g_wake)
        SetEvent(g_wake);
}

static DWORD WINAPI rumble_worker(void *unused)
{
    (void)unused;
    const struct idas_io_backend *backend = wmmt3_input_backend();
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
        bool pulse = g_wantPulse;
        g_wantMotor = -1;
        g_wantPulse = false;
        LeaveCriticalSection(&g_lock);

        if (motor >= 0 && backend->ffb_req_toggle)
        {
            backend->ffb_req_toggle(motor != 0);
            if (!motor)
                pulsing = false;
        }

        if (pulse && backend->ffb_req_rumble)
        {
            if (!pulsing)
                backend->ffb_req_rumble(0, 127);
            pulsing = true;
            pulseEnd = GetTickCount64() + (ULONGLONG)Wmmt3Config.xinput_rumble_ms;
        }
        else if (pulsing && GetTickCount64() >= pulseEnd && backend->ffb_req_rumble)
        {
            backend->ffb_req_rumble(0, 0);
            pulsing = false;
        }
    }
    return 0;
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
    if (Wmmt3Config.input_mode != INPUT_XINPUT) {
        wmmt3_di_ffb_motor(running);
        return;
    }
    if (!g_started)
        return;
    EnterCriticalSection(&g_lock);
    g_motorRunning = running;
    g_wantMotor = running ? 1 : 0;
    if (!running)
        g_wantPulse = false;
    LeaveCriticalSection(&g_lock);
    post();
}

void wmmt3_ffb_init(void)
{
    if (Wmmt3Config.input_mode != INPUT_XINPUT) {
        bool started = wmmt3_di_ffb_init();
        log_info(plfWmmt3, "Force feedback: %s", started ? "on (STRPCB DirectInput)" : "off");
        return;
    }
    const struct idas_io_backend *backend = wmmt3_input_backend();
    if (backend && backend->ffb_init)
        backend->ffb_init();

    bool wanted = Wmmt3Config.xinput_rumble > 0 && Wmmt3Config.xinput_rumble_ms > 0;
    if (wanted &&
        !wmmt3_hook("_ZNK2yt9SAreaData8CheckGapEff", (void *)hook_CheckGap, (void **)&next_CheckGap))
    {
        log_warn(plfWmmt3, "Gamepad rumble: yt::SAreaData::CheckGap not found; road joints will not rumble");
        wanted = false;
    }

    if (backend && wanted)
    {
        InitializeCriticalSection(&g_lock);
        g_wake = CreateEvent(NULL, FALSE, FALSE, NULL);
        HANDLE thread = g_wake ? CreateThread(NULL, 0, rumble_worker, NULL, 0, NULL) : NULL;
        if (thread)
        {
            SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);
            CloseHandle(thread);
            g_started = true;
        }
    }
    log_info(plfWmmt3, "Gamepad rumble: %s (road joints, %d%%, %d ms)",
             g_started ? "on" : "off", Wmmt3Config.xinput_rumble, Wmmt3Config.xinput_rumble_ms);
}
