// ============================================================
// pthreadCond.c - Condition Variable Emulation
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "pthreadEmu.h"
#include "pthreadInternal.h"
#include <stdlib.h>

// ============================================================
// Cond Attribute Functions
// ============================================================

int pthreadEmu_pthreadCondattrInit(void* attr) {
    if (!attr) return LINUX_EINVAL;

    PthreadCondAttrInternal* a = (PthreadCondAttrInternal *)calloc(1, sizeof(PthreadCondAttrInternal));
    a->pshared = LINUX_PTHREAD_PROCESS_PRIVATE;
    a->clock_id = 0;  // CLOCK_REALTIME

    *(void**)attr = a;
    return 0;
}

int pthreadEmu_pthreadCondattrDestroy(void* attr) {
    if (!attr) return LINUX_EINVAL;

    PthreadCondAttrInternal* a = *(PthreadCondAttrInternal**)attr;
    if (a) {
        free(a);
        *(void**)attr = NULL;
    }
    return 0;
}

// The following condattr functions are not in pthreadEmu.h, but are kept
// here for completeness (they will compile but are not registered).

int pthreadEmu_pthreadCondattrSetpshared(void* attr, int pshared) {
    if (!attr) return LINUX_EINVAL;

    PthreadCondAttrInternal* a = *(PthreadCondAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    if (pshared != LINUX_PTHREAD_PROCESS_PRIVATE) {
        return LINUX_EINVAL;
    }

    a->pshared = pshared;
    return 0;
}

int pthreadEmu_pthreadCondattrGetpshared(const void* attr, int* pshared) {
    if (!attr || !pshared) return LINUX_EINVAL;

    PthreadCondAttrInternal* a = *(PthreadCondAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    *pshared = a->pshared;
    return 0;
}

int pthreadEmu_pthreadCondattrSetclock(void* attr, int clock_id) {
    if (!attr) return LINUX_EINVAL;

    PthreadCondAttrInternal* a = *(PthreadCondAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    // Accept but ignore - Windows uses system time
    a->clock_id = clock_id;
    return 0;
}

int pthreadEmu_pthreadCondattrGetclock(const void* attr, int* clock_id) {
    if (!attr || !clock_id) return LINUX_EINVAL;

    PthreadCondAttrInternal* a = *(PthreadCondAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    *clock_id = a->clock_id;
    return 0;
}

// ============================================================
// Condition Variable Functions
// ============================================================

int pthreadEmu_pthreadCondInit(void* cond, const void* attr) {
    if (!cond) return LINUX_EINVAL;

    PthreadCondInternal* c = pthreadMapper_GetOrCreateCond(cond);
    if (!c) return LINUX_ENOMEM;

    if (attr) {
        PthreadCondAttrInternal* a = *(PthreadCondAttrInternal**)attr;
        if (a) {
            c->clock_id = a->clock_id;
        }
    }

    return 0;
}

int pthreadEmu_pthreadCondDestroy(void* cond) {
    if (!cond) return LINUX_EINVAL;

    // CONDITION_VARIABLE doesn't need explicit destruction
    pthreadMapper_DestroyCond(cond);
    return 0;
}

int pthreadEmu_pthreadCondWait(void* cond, void* mutex) {
    if (!cond || !mutex) return LINUX_EINVAL;

    PthreadCondInternal* c = pthreadMapper_GetOrCreateCond(cond);
    PthreadMutexInternal* m = pthreadMapper_GetOrCreateMutex(mutex);

    if (!c || !m) return LINUX_EINVAL;

    // Save and clear lock state for recursive mutexes
    int saved_count = m->lock_count;
    // DWORD saved_owner = m->owner_thread;
    m->lock_count = 0;
    m->owner_thread = 0;

    // Wait on condition
    BOOL result = SleepConditionVariableCS(&c->cv, &m->cs, INFINITE);

    // Restore lock state
    m->owner_thread = GetCurrentThreadId();
    m->lock_count = saved_count;

    return result ? 0 : LINUX_EINVAL;
}

int pthreadEmu_pthreadCondTimedwait(void* cond, void* mutex,
                                        const struct timespec* abstime) {
    if (!cond || !mutex) return LINUX_EINVAL;
    if (!abstime) return pthreadEmu_pthreadCondWait(cond, mutex);

    PthreadCondInternal* c = pthreadMapper_GetOrCreateCond(cond);
    PthreadMutexInternal* m = pthreadMapper_GetOrCreateMutex(mutex);

    if (!c || !m) return LINUX_EINVAL;

    DWORD timeout_ms = TimespecToMilliseconds(abstime);

    // Save and clear lock state for recursive mutexes
    int saved_count = m->lock_count;
    // DWORD saved_owner = m->owner_thread;
    m->lock_count = 0;
    m->owner_thread = 0;

    // Wait on condition with timeout
    BOOL result = SleepConditionVariableCS(&c->cv, &m->cs, timeout_ms);
    DWORD error = result ? 0 : GetLastError();

    // Restore lock state
    m->owner_thread = GetCurrentThreadId();
    m->lock_count = saved_count;

    if (!result) {
        if (error == ERROR_TIMEOUT) {
            return LINUX_ETIMEDOUT;
        }
        return LINUX_EINVAL;
    }

    return 0;
}

int pthreadEmu_pthreadCondSignal(void* cond) {
    if (!cond) return LINUX_EINVAL;

    PthreadCondInternal* c = pthreadMapper_GetOrCreateCond(cond);
    if (!c) return LINUX_EINVAL;

    WakeConditionVariable(&c->cv);
    return 0;
}

int pthreadEmu_pthreadCondBroadcast(void* cond) {
    if (!cond) return LINUX_EINVAL;

    PthreadCondInternal* c = pthreadMapper_GetOrCreateCond(cond);
    if (!c) return LINUX_EINVAL;

    WakeAllConditionVariable(&c->cv);
    return 0;
}
