#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "pthreadInternal.h"
#include "../collections.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================ */
/*  Static members                                              */
/* ============================================================ */

static SRWLOCK s_lock = SRWLOCK_INIT;
static volatile int s_initialized = 0;

/* Mapping tables (replacing std::unordered_map) */
static PtrMap s_mutex_map;   /* void* -> PthreadMutexInternal* */
static PtrMap s_cond_map;    /* void* -> PthreadCondInternal* */
static PtrMap s_rwlock_map;  /* void* -> PthreadRwlockInternal* */
static PtrMap s_barrier_map; /* void* -> PthreadBarrierInternal* */
static PtrMap s_spin_map;    /* void* -> PthreadSpinInternal* */
static IntMap s_thread_map;  /* uint32_t -> PthreadThreadInternal* */
/* s_wintid_to_linuxtid: DWORD -> uint32_t. Store as IntMap with DWORD cast to int32_t. */
static IntMap s_wintid_to_linuxtid;

/* Generates unique Linux thread IDs */
static volatile uint32_t s_next_thread_id = 1000;

/* ============================================================ */
/*  Initialize / Shutdown                                       */
/* ============================================================ */

void pthreadMapper_Initialize(void)
{
    if (s_initialized)
        return;

    InitializeSRWLock(&s_lock);
    ptrMap_Init(&s_mutex_map);
    ptrMap_Init(&s_cond_map);
    ptrMap_Init(&s_rwlock_map);
    ptrMap_Init(&s_barrier_map);
    ptrMap_Init(&s_spin_map);
    intMap_Init(&s_thread_map);
    intMap_Init(&s_wintid_to_linuxtid);
    s_initialized = 1;

    uint32_t main_tid;
    PthreadThreadInternal *main_thread = pthreadMapper_CreateThread(&main_tid);
    if (main_thread)
    {
        main_thread->handle = GetCurrentThread();
        main_thread->win_thread_id = GetCurrentThreadId();
        main_thread->detached = 0;
        main_thread->exited = 0;

        AcquireSRWLockExclusive(&s_lock);
        intMap_Set(&s_wintid_to_linuxtid, (int32_t)main_thread->win_thread_id, (void *)(uintptr_t)main_tid);
        ReleaseSRWLockExclusive(&s_lock);
    }
}

/* Callback helpers for shutdown cleanup */
static void cleanupMutex(void *key, void *value, void *ud)
{
    (void)key; (void)ud;
    PthreadMutexInternal *m = (PthreadMutexInternal *)value;
    if (m)
    {
        DeleteCriticalSection(&m->cs);
        free(m);
    }
}

static void cleanupCond(void *key, void *value, void *ud)
{
    (void)key; (void)ud;
    PthreadCondInternal *c = (PthreadCondInternal *)value;
    if (c)
        free(c);
}

static void cleanupRwlock(void *key, void *value, void *ud)
{
    (void)key; (void)ud;
    PthreadRwlockInternal *r = (PthreadRwlockInternal *)value;
    if (r)
        free(r);
}

static void cleanupBarrier(void *key, void *value, void *ud)
{
    (void)key; (void)ud;
    PthreadBarrierInternal *b = (PthreadBarrierInternal *)value;
    if (b)
    {
        DeleteCriticalSection(&b->cs);
        free(b);
    }
}

static void cleanupSpin(void *key, void *value, void *ud)
{
    (void)key; (void)ud;
    PthreadSpinInternal *s = (PthreadSpinInternal *)value;
    if (s)
        free(s);
}

static void cleanupThread(int32_t key, void *value, void *ud)
{
    (void)key; (void)ud;
    PthreadThreadInternal *t = (PthreadThreadInternal *)value;
    if (t)
    {
        if (t->handle && t->handle != INVALID_HANDLE_VALUE)
        {
            if (t->handle != GetCurrentThread())
            {
                CloseHandle(t->handle);
            }
        }
        free(t);
    }
}

void pthreadMapper_Shutdown(void)
{
    if (!s_initialized)
        return;

    AcquireSRWLockExclusive(&s_lock);

    ptrMap_ForEach(&s_mutex_map, cleanupMutex, NULL);
    ptrMap_Clear(&s_mutex_map);

    ptrMap_ForEach(&s_cond_map, cleanupCond, NULL);
    ptrMap_Clear(&s_cond_map);

    ptrMap_ForEach(&s_rwlock_map, cleanupRwlock, NULL);
    ptrMap_Clear(&s_rwlock_map);

    ptrMap_ForEach(&s_barrier_map, cleanupBarrier, NULL);
    ptrMap_Clear(&s_barrier_map);

    ptrMap_ForEach(&s_spin_map, cleanupSpin, NULL);
    ptrMap_Clear(&s_spin_map);

    intMap_ForEach(&s_thread_map, cleanupThread, NULL);
    intMap_Clear(&s_thread_map);
    intMap_Clear(&s_wintid_to_linuxtid);

    ReleaseSRWLockExclusive(&s_lock);
    s_initialized = 0;
}

/* ============================================================ */
/*  Mutex Mapping                                               */
/* ============================================================ */

PthreadMutexInternal *pthreadMapper_GetOrCreateMutex(void *linux_mutex)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadMutexInternal *existing = (PthreadMutexInternal *)ptrMap_Get(&s_mutex_map, linux_mutex);
    if (existing && existing->initialized == MUTEX_INIT_MAGIC)
    {
        ReleaseSRWLockExclusive(&s_lock);
        return existing;
    }

    PthreadMutexInternal *mutex = (PthreadMutexInternal *)calloc(1, sizeof(PthreadMutexInternal));
    InitializeCriticalSection(&mutex->cs);
    mutex->type = LINUX_PTHREAD_MUTEX_DEFAULT;
    mutex->owner_thread = 0;
    mutex->lock_count = 0;
    mutex->initialized = MUTEX_INIT_MAGIC;

    ptrMap_Set(&s_mutex_map, linux_mutex, mutex);

    ReleaseSRWLockExclusive(&s_lock);
    return mutex;
}

PthreadMutexInternal *pthreadMapper_FindMutex(void *linux_mutex)
{
    AcquireSRWLockShared(&s_lock);

    PthreadMutexInternal *result = NULL;
    PthreadMutexInternal *existing = (PthreadMutexInternal *)ptrMap_Get(&s_mutex_map, linux_mutex);
    if (existing && existing->initialized == MUTEX_INIT_MAGIC)
        result = existing;

    ReleaseSRWLockShared(&s_lock);
    return result;
}

void pthreadMapper_DestroyMutex(void *linux_mutex)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadMutexInternal *existing = (PthreadMutexInternal *)ptrMap_Get(&s_mutex_map, linux_mutex);
    if (existing)
    {
        DeleteCriticalSection(&existing->cs);
        existing->initialized = 0;
        free(existing);
        ptrMap_Remove(&s_mutex_map, linux_mutex);
    }

    ReleaseSRWLockExclusive(&s_lock);
}

/* ============================================================ */
/*  Cond Mapping                                                */
/* ============================================================ */

PthreadCondInternal *pthreadMapper_GetOrCreateCond(void *linux_cond)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadCondInternal *existing = (PthreadCondInternal *)ptrMap_Get(&s_cond_map, linux_cond);
    if (existing && existing->initialized == COND_INIT_MAGIC)
    {
        ReleaseSRWLockExclusive(&s_lock);
        return existing;
    }

    PthreadCondInternal *cond = (PthreadCondInternal *)calloc(1, sizeof(PthreadCondInternal));
    InitializeConditionVariable(&cond->cv);
    cond->clock_id = 0; /* CLOCK_REALTIME */
    cond->initialized = COND_INIT_MAGIC;

    ptrMap_Set(&s_cond_map, linux_cond, cond);

    ReleaseSRWLockExclusive(&s_lock);
    return cond;
}

PthreadCondInternal *pthreadMapper_FindCond(void *linux_cond)
{
    AcquireSRWLockShared(&s_lock);

    PthreadCondInternal *result = NULL;
    PthreadCondInternal *existing = (PthreadCondInternal *)ptrMap_Get(&s_cond_map, linux_cond);
    if (existing && existing->initialized == COND_INIT_MAGIC)
        result = existing;

    ReleaseSRWLockShared(&s_lock);
    return result;
}

void pthreadMapper_DestroyCond(void *linux_cond)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadCondInternal *existing = (PthreadCondInternal *)ptrMap_Get(&s_cond_map, linux_cond);
    if (existing)
    {
        existing->initialized = 0;
        free(existing);
        ptrMap_Remove(&s_cond_map, linux_cond);
    }

    ReleaseSRWLockExclusive(&s_lock);
}

/* ============================================================ */
/*  Rwlock Mapping                                              */
/* ============================================================ */

PthreadRwlockInternal *pthreadMapper_GetOrCreateRwlock(void *linux_rwlock)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadRwlockInternal *existing = (PthreadRwlockInternal *)ptrMap_Get(&s_rwlock_map, linux_rwlock);
    if (existing && existing->initialized == RWLOCK_INIT_MAGIC)
    {
        ReleaseSRWLockExclusive(&s_lock);
        return existing;
    }

    PthreadRwlockInternal *rwlock = (PthreadRwlockInternal *)calloc(1, sizeof(PthreadRwlockInternal));
    InitializeSRWLock(&rwlock->srw);
    rwlock->initialized = RWLOCK_INIT_MAGIC;

    ptrMap_Set(&s_rwlock_map, linux_rwlock, rwlock);

    ReleaseSRWLockExclusive(&s_lock);
    return rwlock;
}

PthreadRwlockInternal *pthreadMapper_FindRwlock(void *linux_rwlock)
{
    AcquireSRWLockShared(&s_lock);

    PthreadRwlockInternal *result = NULL;
    PthreadRwlockInternal *existing = (PthreadRwlockInternal *)ptrMap_Get(&s_rwlock_map, linux_rwlock);
    if (existing && existing->initialized == RWLOCK_INIT_MAGIC)
        result = existing;

    ReleaseSRWLockShared(&s_lock);
    return result;
}

void pthreadMapper_DestroyRwlock(void *linux_rwlock)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadRwlockInternal *existing = (PthreadRwlockInternal *)ptrMap_Get(&s_rwlock_map, linux_rwlock);
    if (existing)
    {
        existing->initialized = 0;
        free(existing);
        ptrMap_Remove(&s_rwlock_map, linux_rwlock);
    }

    ReleaseSRWLockExclusive(&s_lock);
}

/* ============================================================ */
/*  Barrier Mapping                                             */
/* ============================================================ */

PthreadBarrierInternal *pthreadMapper_GetOrCreateBarrier(void *linux_barrier, unsigned int count)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadBarrierInternal *existing = (PthreadBarrierInternal *)ptrMap_Get(&s_barrier_map, linux_barrier);
    if (existing && existing->initialized == BARRIER_INIT_MAGIC)
    {
        ReleaseSRWLockExclusive(&s_lock);
        return existing;
    }

    PthreadBarrierInternal *barrier = (PthreadBarrierInternal *)calloc(1, sizeof(PthreadBarrierInternal));
    InitializeCriticalSection(&barrier->cs);
    InitializeConditionVariable(&barrier->cv);
    barrier->threshold = count;
    barrier->count = 0;
    barrier->generation = 0;
    barrier->initialized = BARRIER_INIT_MAGIC;

    ptrMap_Set(&s_barrier_map, linux_barrier, barrier);

    ReleaseSRWLockExclusive(&s_lock);
    return barrier;
}

PthreadBarrierInternal *pthreadMapper_FindBarrier(void *linux_barrier)
{
    AcquireSRWLockShared(&s_lock);

    PthreadBarrierInternal *result = NULL;
    PthreadBarrierInternal *existing = (PthreadBarrierInternal *)ptrMap_Get(&s_barrier_map, linux_barrier);
    if (existing && existing->initialized == BARRIER_INIT_MAGIC)
        result = existing;

    ReleaseSRWLockShared(&s_lock);
    return result;
}

void pthreadMapper_DestroyBarrier(void *linux_barrier)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadBarrierInternal *existing = (PthreadBarrierInternal *)ptrMap_Get(&s_barrier_map, linux_barrier);
    if (existing)
    {
        DeleteCriticalSection(&existing->cs);
        existing->initialized = 0;
        free(existing);
        ptrMap_Remove(&s_barrier_map, linux_barrier);
    }

    ReleaseSRWLockExclusive(&s_lock);
}

/* ============================================================ */
/*  Spin Mapping                                                */
/* ============================================================ */

PthreadSpinInternal *pthreadMapper_GetOrCreateSpin(void *linux_spin)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadSpinInternal *existing = (PthreadSpinInternal *)ptrMap_Get(&s_spin_map, linux_spin);
    if (existing && existing->initialized == SPIN_INIT_MAGIC)
    {
        ReleaseSRWLockExclusive(&s_lock);
        return existing;
    }

    PthreadSpinInternal *spin = (PthreadSpinInternal *)calloc(1, sizeof(PthreadSpinInternal));
    spin->lock = 0;
    spin->initialized = SPIN_INIT_MAGIC;

    ptrMap_Set(&s_spin_map, linux_spin, spin);

    ReleaseSRWLockExclusive(&s_lock);
    return spin;
}

PthreadSpinInternal *pthreadMapper_FindSpin(void *linux_spin)
{
    AcquireSRWLockShared(&s_lock);

    PthreadSpinInternal *result = NULL;
    PthreadSpinInternal *existing = (PthreadSpinInternal *)ptrMap_Get(&s_spin_map, linux_spin);
    if (existing && existing->initialized == SPIN_INIT_MAGIC)
        result = existing;

    ReleaseSRWLockShared(&s_lock);
    return result;
}

void pthreadMapper_DestroySpin(void *linux_spin)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadSpinInternal *existing = (PthreadSpinInternal *)ptrMap_Get(&s_spin_map, linux_spin);
    if (existing)
    {
        existing->initialized = 0;
        free(existing);
        ptrMap_Remove(&s_spin_map, linux_spin);
    }

    ReleaseSRWLockExclusive(&s_lock);
}

/* ============================================================ */
/*  Thread Mapping                                              */
/* ============================================================ */

PthreadThreadInternal *pthreadMapper_CreateThread(uint32_t *out_linux_tid)
{
    AcquireSRWLockExclusive(&s_lock);

    uint32_t tid = (uint32_t)InterlockedIncrement((volatile LONG *)&s_next_thread_id);

    PthreadThreadInternal *thread = (PthreadThreadInternal *)calloc(1, sizeof(PthreadThreadInternal));
    thread->linux_thread_id = tid;

    intMap_Set(&s_thread_map, (int32_t)tid, thread);

    if (out_linux_tid)
        *out_linux_tid = tid;

    ReleaseSRWLockExclusive(&s_lock);
    return thread;
}

PthreadThreadInternal *pthreadMapper_FindThread(uint32_t linux_tid)
{
    AcquireSRWLockShared(&s_lock);

    PthreadThreadInternal *result = (PthreadThreadInternal *)intMap_Get(&s_thread_map, (int32_t)linux_tid);

    ReleaseSRWLockShared(&s_lock);
    return result;
}

PthreadThreadInternal *pthreadMapper_FindThreadByWinId(DWORD win_tid)
{
    AcquireSRWLockShared(&s_lock);

    PthreadThreadInternal *result = NULL;
    uint32_t linux_tid = (uint32_t)(uintptr_t)intMap_Get(&s_wintid_to_linuxtid, (int32_t)win_tid);
    if (linux_tid != 0)
    {
        result = (PthreadThreadInternal *)intMap_Get(&s_thread_map, (int32_t)linux_tid);
    }

    ReleaseSRWLockShared(&s_lock);
    return result;
}

void pthreadMapper_DestroyThread(uint32_t linux_tid)
{
    AcquireSRWLockExclusive(&s_lock);

    PthreadThreadInternal *thread = (PthreadThreadInternal *)intMap_Get(&s_thread_map, (int32_t)linux_tid);
    if (thread)
    {
        /* Remove from wintid map */
        intMap_Remove(&s_wintid_to_linuxtid, (int32_t)thread->win_thread_id);

        if (thread->handle && thread->handle != INVALID_HANDLE_VALUE &&
            thread->handle != GetCurrentThread())
        {
            CloseHandle(thread->handle);
        }
        free(thread);
        intMap_Remove(&s_thread_map, (int32_t)linux_tid);
    }

    ReleaseSRWLockExclusive(&s_lock);
}

uint32_t pthreadMapper_GetCurrentLinuxTid(void)
{
    DWORD win_tid = GetCurrentThreadId();

    AcquireSRWLockShared(&s_lock);

    uint32_t result = (uint32_t)(uintptr_t)intMap_Get(&s_wintid_to_linuxtid, (int32_t)win_tid);

    ReleaseSRWLockShared(&s_lock);

    /* If not found, this thread wasn't created via pthread_create.
     * Create an entry for it. */
    if (result == 0)
    {
        uint32_t new_tid;
        PthreadThreadInternal *thread = pthreadMapper_CreateThread(&new_tid);
        if (thread)
        {
            thread->handle = GetCurrentThread();
            thread->win_thread_id = win_tid;
            thread->detached = 1; /* Treat as detached */

            AcquireSRWLockExclusive(&s_lock);
            intMap_Set(&s_wintid_to_linuxtid, (int32_t)win_tid, (void *)(uintptr_t)new_tid);
            ReleaseSRWLockExclusive(&s_lock);

            result = new_tid;
        }
    }

    return result;
}
