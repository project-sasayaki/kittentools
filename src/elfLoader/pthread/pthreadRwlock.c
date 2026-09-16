// ============================================================
// pthread_rwlock.c - Read-Write Lock Emulation
// ============================================================

#include "pthreadEmu.h"
#include "pthreadInternal.h"

// ============================================================
// TLS for tracking read/write lock state per thread
// ============================================================

static DWORD s_tls_rwlock_state = TLS_OUT_OF_INDEXES;

#define RWLOCK_STATE_NONE   0
#define RWLOCK_STATE_READ   1
#define RWLOCK_STATE_WRITE  2

struct RwlockStateMap {
    void* rwlock;
    int state;
    struct RwlockStateMap* next;
};

static void InitRwlockTLS(void) {
    if (s_tls_rwlock_state == TLS_OUT_OF_INDEXES) {
        s_tls_rwlock_state = TlsAlloc();
    }
}

static struct RwlockStateMap* GetRwlockStateList(void) {
    InitRwlockTLS();
    return (struct RwlockStateMap*)TlsGetValue(s_tls_rwlock_state);
}

static void SetRwlockState(void* rwlock, int state) {
    InitRwlockTLS();

    struct RwlockStateMap* list = GetRwlockStateList();

    // Find existing entry
    for (struct RwlockStateMap* p = list; p; p = p->next) {
        if (p->rwlock == rwlock) {
            p->state = state;
            return;
        }
    }

    // Add new entry
    struct RwlockStateMap* entry = (struct RwlockStateMap*)calloc(1, sizeof(struct RwlockStateMap));
    entry->rwlock = rwlock;
    entry->state = state;
    entry->next = list;
    TlsSetValue(s_tls_rwlock_state, entry);
}

static int GetRwlockState(void* rwlock) {
    struct RwlockStateMap* list = GetRwlockStateList();

    for (struct RwlockStateMap* p = list; p; p = p->next) {
        if (p->rwlock == rwlock) {
            return p->state;
        }
    }

    return RWLOCK_STATE_NONE;
}

static void ClearRwlockState(void* rwlock) {
    SetRwlockState(rwlock, RWLOCK_STATE_NONE);
}

// ============================================================
// Rwlock Attribute Functions
// ============================================================

int pthreadEmu_pthreadRwlockattrInit(void* attr) {
    if (!attr) return LINUX_EINVAL;

    PthreadRwlockAttrInternal* a = (PthreadRwlockAttrInternal*)calloc(1, sizeof(PthreadRwlockAttrInternal));
    a->pshared = LINUX_PTHREAD_PROCESS_PRIVATE;

    *(void**)attr = a;
    return 0;
}

int pthreadEmu_pthreadRwlockattrDestroy(void* attr) {
    if (!attr) return LINUX_EINVAL;

    PthreadRwlockAttrInternal* a = *(PthreadRwlockAttrInternal**)attr;
    if (a) {
        free(a);
        *(void**)attr = NULL;
    }
    return 0;
}

int pthreadEmu_pthreadRwlockattrSetpshared(void* attr, int pshared) {
    if (!attr) return LINUX_EINVAL;

    PthreadRwlockAttrInternal* a = *(PthreadRwlockAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    if (pshared != LINUX_PTHREAD_PROCESS_PRIVATE) {
        return LINUX_EINVAL;
    }

    a->pshared = pshared;
    return 0;
}

int pthreadEmu_pthreadRwlockattrGetpshared(const void* attr, int* pshared) {
    if (!attr || !pshared) return LINUX_EINVAL;

    PthreadRwlockAttrInternal* a = *(PthreadRwlockAttrInternal**)attr;
    if (!a) return LINUX_EINVAL;

    *pshared = a->pshared;
    return 0;
}

// ============================================================
// Rwlock Functions
// ============================================================

int pthreadEmu_pthreadRwlockInit(void* rwlock, const void* attr) {
    if (!rwlock) return LINUX_EINVAL;

    PthreadRwlockInternal* rw = pthreadMapper_GetOrCreateRwlock(rwlock);
    if (!rw) return LINUX_ENOMEM;

    // SRWLOCK is initialized in GetOrCreateRwlock
    return 0;
}

int pthreadEmu_pthreadRwlockDestroy(void* rwlock) {
    if (!rwlock) return LINUX_EINVAL;

    // SRWLOCK doesn't need explicit destruction
    pthreadMapper_DestroyRwlock(rwlock);
    return 0;
}

int pthreadEmu_pthreadRwlockRdlock(void* rwlock) {
    if (!rwlock) return LINUX_EINVAL;

    PthreadRwlockInternal* rw = pthreadMapper_GetOrCreateRwlock(rwlock);
    if (!rw) return LINUX_EINVAL;

    AcquireSRWLockShared(&rw->srw);
    SetRwlockState(rwlock, RWLOCK_STATE_READ);

    return 0;
}

int pthreadEmu_pthreadRwlockTryrdlock(void* rwlock) {
    if (!rwlock) return LINUX_EINVAL;

    PthreadRwlockInternal* rw = pthreadMapper_GetOrCreateRwlock(rwlock);
    if (!rw) return LINUX_EINVAL;

    if (!TryAcquireSRWLockShared(&rw->srw)) {
        return LINUX_EBUSY;
    }

    SetRwlockState(rwlock, RWLOCK_STATE_READ);
    return 0;
}

int pthreadEmu_pthreadRwlockTimedrdlock(void* rwlock, const struct timespec* abstime) {
    if (!rwlock) return LINUX_EINVAL;
    if (!abstime) return pthreadEmu_pthreadRwlockRdlock(rwlock);

    // Windows SRWLOCK doesn't have timed acquire
    // Implement with spin + sleep
    DWORD timeout_ms = TimespecToMilliseconds(abstime);
    DWORD start = GetTickCount();

    while (1) {
        int result = pthreadEmu_pthreadRwlockTryrdlock(rwlock);
        if (result == 0) {
            return 0;
        }

        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeout_ms) {
            return LINUX_ETIMEDOUT;
        }

        Sleep(1);
    }
}

int pthreadEmu_pthreadRwlockWrlock(void* rwlock) {
    if (!rwlock) return LINUX_EINVAL;

    PthreadRwlockInternal* rw = pthreadMapper_GetOrCreateRwlock(rwlock);
    if (!rw) return LINUX_EINVAL;

    AcquireSRWLockExclusive(&rw->srw);
    SetRwlockState(rwlock, RWLOCK_STATE_WRITE);

    return 0;
}

int pthreadEmu_pthreadRwlockTrywrlock(void* rwlock) {
    if (!rwlock) return LINUX_EINVAL;

    PthreadRwlockInternal* rw = pthreadMapper_GetOrCreateRwlock(rwlock);
    if (!rw) return LINUX_EINVAL;

    if (!TryAcquireSRWLockExclusive(&rw->srw)) {
        return LINUX_EBUSY;
    }

    SetRwlockState(rwlock, RWLOCK_STATE_WRITE);
    return 0;
}

int pthreadEmu_pthreadRwlockTimedwrlock(void* rwlock, const struct timespec* abstime) {
    if (!rwlock) return LINUX_EINVAL;
    if (!abstime) return pthreadEmu_pthreadRwlockWrlock(rwlock);

    DWORD timeout_ms = TimespecToMilliseconds(abstime);
    DWORD start = GetTickCount();

    while (1) {
        int result = pthreadEmu_pthreadRwlockTrywrlock(rwlock);
        if (result == 0) {
            return 0;
        }

        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeout_ms) {
            return LINUX_ETIMEDOUT;
        }

        Sleep(1);
    }
}

int pthreadEmu_pthreadRwlockUnlock(void* rwlock) {
    if (!rwlock) return LINUX_EINVAL;

    PthreadRwlockInternal* rw = pthreadMapper_FindRwlock(rwlock);
    if (!rw) return LINUX_EINVAL;

    int state = GetRwlockState(rwlock);

    switch (state) {
        case RWLOCK_STATE_READ:
            ReleaseSRWLockShared(&rw->srw);
            break;

        case RWLOCK_STATE_WRITE:
            ReleaseSRWLockExclusive(&rw->srw);
            break;

        default:
            // Not locked by this thread
            return LINUX_EPERM;
    }

    ClearRwlockState(rwlock);
    return 0;
}
