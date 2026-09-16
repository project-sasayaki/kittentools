// ============================================================
// pthreadMutex.c - Mutex Emulation
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
// Mutex Attribute Functions
// ============================================================

int pthreadEmu_pthreadMutexattrInit(void* attr) {
    if (!attr) return LINUX_EINVAL;

    PthreadMutexAttrInternal* a = (PthreadMutexAttrInternal *)calloc(1, sizeof(PthreadMutexAttrInternal));
    a->type = LINUX_PTHREAD_MUTEX_DEFAULT;
    a->protocol = LINUX_PTHREAD_PRIO_NONE;
    a->pshared = LINUX_PTHREAD_PROCESS_PRIVATE;

    // Store pointer in Linux attr space
    *(void**)attr = a;
    return 0;
}

int pthreadEmu_pthreadMutexattrDestroy(void* attr) {
    if (!attr) return LINUX_EINVAL;

    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (a) {
        free(a);
        *(void**)attr = NULL;
    }
    return 0;
}

int pthreadEmu_pthreadMutexattrSettype(void* attr, int type) {
    if (!attr) return LINUX_EINVAL;

    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    if (type != LINUX_PTHREAD_MUTEX_NORMAL &&
        type != LINUX_PTHREAD_MUTEX_RECURSIVE &&
        type != LINUX_PTHREAD_MUTEX_ERRORCHECK) {
        return LINUX_EINVAL;
    }

    a->type = type;
    return 0;
}

int pthreadEmu_pthreadMutexattrGettype(const void* attr, int* type) {
    if (!attr || !type) return LINUX_EINVAL;

    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    *type = a->type;
    return 0;
}

// pshared attr functions are not in pthreadEmu.h, but kept for completeness.
int pthreadEmu_pthreadMutexattrSetpshared(void* attr, int pshared) {
    if (!attr) return LINUX_EINVAL;

    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    // Only PRIVATE is supported
    if (pshared != LINUX_PTHREAD_PROCESS_PRIVATE) {
        return LINUX_EINVAL;
    }

    a->pshared = pshared;
    return 0;
}

int pthreadEmu_pthreadMutexattrGetpshared(const void* attr, int* pshared) {
    if (!attr || !pshared) return LINUX_EINVAL;

    PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    *pshared = a->pshared;
    return 0;
}

// ============================================================
// Mutex Functions
// ============================================================

int pthreadEmu_pthreadMutexInit(void* mutex, const void* attr) {
    if (!mutex) return LINUX_EINVAL;

    // Get or create mutex
    PthreadMutexInternal* m = pthreadMapper_GetOrCreateMutex(mutex);
    if (!m) return LINUX_ENOMEM;

    // Apply attributes if provided
    if (attr) {
        PthreadMutexAttrInternal* a = *(PthreadMutexAttrInternal**)attr;
        if (a) {
            m->type = a->type;
        }
    }

    return 0;
}

int pthreadEmu_pthreadMutexDestroy(void* mutex) {
    if (!mutex) return LINUX_EINVAL;

    PthreadMutexInternal* m = pthreadMapper_FindMutex(mutex);
    if (!m) {
        // Already destroyed or never initialized - treat as success
        return 0;
    }

    // Check if locked
    if (m->lock_count > 0) {
        return LINUX_EBUSY;
    }

    pthreadMapper_DestroyMutex(mutex);
    return 0;
}

int pthreadEmu_pthreadMutexLock(void* mutex) {
    if (!mutex) return LINUX_EINVAL;

    PthreadMutexInternal* m = pthreadMapper_GetOrCreateMutex(mutex);
    if (!m) return LINUX_EINVAL;

    DWORD current_thread = GetCurrentThreadId();

    switch (m->type) {
        case LINUX_PTHREAD_MUTEX_NORMAL:
            // Standard behavior - just lock
            EnterCriticalSection(&m->cs);
            m->owner_thread = current_thread;
            m->lock_count = 1;
            break;

        case LINUX_PTHREAD_MUTEX_RECURSIVE:
            // Allow recursive locking
            EnterCriticalSection(&m->cs);
            m->owner_thread = current_thread;
            m->lock_count++;
            break;

        case LINUX_PTHREAD_MUTEX_ERRORCHECK:
            // Check for deadlock
            if (m->owner_thread == current_thread && m->lock_count > 0) {
                return LINUX_EDEADLK;
            }
            EnterCriticalSection(&m->cs);
            m->owner_thread = current_thread;
            m->lock_count = 1;
            break;
    }

    return 0;
}

int pthreadEmu_pthreadMutexTrylock(void* mutex) {
    if (!mutex) return LINUX_EINVAL;

    PthreadMutexInternal* m = pthreadMapper_GetOrCreateMutex(mutex);
    if (!m) return LINUX_EINVAL;

    DWORD current_thread = GetCurrentThreadId();

    switch (m->type) {
        case LINUX_PTHREAD_MUTEX_NORMAL:
            if (!TryEnterCriticalSection(&m->cs)) {
                return LINUX_EBUSY;
            }
            m->owner_thread = current_thread;
            m->lock_count = 1;
            break;

        case LINUX_PTHREAD_MUTEX_RECURSIVE:
            if (!TryEnterCriticalSection(&m->cs)) {
                return LINUX_EBUSY;
            }
            m->owner_thread = current_thread;
            m->lock_count++;
            break;

        case LINUX_PTHREAD_MUTEX_ERRORCHECK:
            if (m->owner_thread == current_thread && m->lock_count > 0) {
                return LINUX_EBUSY;
            }
            if (!TryEnterCriticalSection(&m->cs)) {
                return LINUX_EBUSY;
            }
            m->owner_thread = current_thread;
            m->lock_count = 1;
            break;
    }

    return 0;
}

int pthreadEmu_pthreadMutexTimedlock(void* mutex, const struct timespec* abstime) {
    if (!mutex) return LINUX_EINVAL;
    if (!abstime) return pthreadEmu_pthreadMutexLock(mutex);  // No timeout = regular lock

    PthreadMutexInternal* m = pthreadMapper_GetOrCreateMutex(mutex);
    if (!m) return LINUX_EINVAL;

    DWORD timeout_ms = TimespecToMilliseconds(abstime);

    if (timeout_ms == 0) {
        // Already timed out
        return pthreadEmu_pthreadMutexTrylock(mutex);
    }

    // Spin with timeout
    DWORD start = GetTickCount();

    while (1) {
        int result = pthreadEmu_pthreadMutexTrylock(mutex);
        if (result == 0) {
            return 0;  // Got the lock
        }

        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeout_ms) {
            return LINUX_ETIMEDOUT;
        }

        // Sleep a bit before retry
        Sleep(1);
    }
}

int pthreadEmu_pthreadMutexUnlock(void* mutex) {
    if (!mutex) return LINUX_EINVAL;

    PthreadMutexInternal* m = pthreadMapper_FindMutex(mutex);
    if (!m) {
        // Unknown mutex - possibly already destroyed
        return LINUX_EINVAL;
    }

    DWORD current_thread = GetCurrentThreadId();

    switch (m->type) {
        case LINUX_PTHREAD_MUTEX_NORMAL:
            m->owner_thread = 0;
            m->lock_count = 0;
            LeaveCriticalSection(&m->cs);
            break;

        case LINUX_PTHREAD_MUTEX_RECURSIVE:
            if (m->owner_thread != current_thread) {
                return LINUX_EPERM;
            }
            m->lock_count--;
            if (m->lock_count == 0) {
                m->owner_thread = 0;
            }
            LeaveCriticalSection(&m->cs);
            break;

        case LINUX_PTHREAD_MUTEX_ERRORCHECK:
            if (m->owner_thread != current_thread || m->lock_count == 0) {
                return LINUX_EPERM;
            }
            m->owner_thread = 0;
            m->lock_count = 0;
            LeaveCriticalSection(&m->cs);
            break;
    }

    return 0;
}
