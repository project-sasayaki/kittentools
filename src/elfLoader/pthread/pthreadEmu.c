#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "pthreadEmu.h"
#include "pthreadInternal.h"

/* External declaration for TLS destructor support */
extern void CallTlsDestructors(void);

/* ============================================================ */
/*  Global State                                                 */
/* ============================================================ */

static volatile int s_emu_initialized = 0;
static CRITICAL_SECTION s_init_lock;
static volatile int s_init_lock_ready = 0;

/* One-time initialization for the init lock itself */
static void EnsureInitLock(void)
{
    if (!s_init_lock_ready)
    {
        InitializeCriticalSection(&s_init_lock);
        s_init_lock_ready = 1;
    }
}

/* ============================================================ */
/*  Initialize / Shutdown                                        */
/* ============================================================ */

void PthreadEmu_Initialize(void)
{
    EnsureInitLock();

    EnterCriticalSection(&s_init_lock);

    if (s_emu_initialized)
    {
        LeaveCriticalSection(&s_init_lock);
        return;
    }

    /* Initialize the mapper (handles all object tracking) */
    pthreadMapper_Initialize();

    s_emu_initialized = 1;

    LeaveCriticalSection(&s_init_lock);
}

void PthreadEmu_Shutdown(void)
{
    EnsureInitLock();

    EnterCriticalSection(&s_init_lock);

    if (!s_emu_initialized)
    {
        LeaveCriticalSection(&s_init_lock);
        return;
    }

    /* Call TLS destructors for main thread */
    CallTlsDestructors();

    /* Cleanup semaphores */
    SemaphoreCleanup();

    /* Shutdown the mapper (cleans up all objects) */
    pthreadMapper_Shutdown();

    s_emu_initialized = 0;

    LeaveCriticalSection(&s_init_lock);
}
