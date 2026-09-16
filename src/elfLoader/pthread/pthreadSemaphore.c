// ============================================================
// pthreadSemaphore.c - Semaphore Emulation
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "pthreadEmu.h"
#include "pthreadInternal.h"
#include "../collections.h"

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============================================================
// Semaphore Internal Structure
// ============================================================

typedef struct SemaphoreInternal {
    HANDLE              handle;
    volatile LONG       value;
    volatile int        initialized;
    char                name[256];
    volatile LONG       refcount;
} SemaphoreInternal;


#define SEM_INIT_MAGIC  0x53454D41  // "SEMA"
#define SEM_VALUE_MAX   0x7FFFFFFF

// ============================================================
// Semaphore Mapping (similar to mutex mapping)
// ============================================================

static PtrMap s_sem_map;     /* void* -> SemaphoreInternal* */
static StrMap s_named_sems;  /* name   -> SemaphoreInternal* */
static SRWLOCK s_sem_lock = SRWLOCK_INIT;
static int s_sem_maps_init = 0;

/* Lazily initialize the maps. Must be called under s_sem_lock. */
static void EnsureSemMapsInit(void) {
    if (!s_sem_maps_init) {
        ptrMap_Init(&s_sem_map);
        strMap_Init(&s_named_sems);
        s_sem_maps_init = 1;
    }
}

static SemaphoreInternal* GetOrCreateSem(void* linux_sem, unsigned int initial_value) {
    AcquireSRWLockExclusive(&s_sem_lock);
    EnsureSemMapsInit();

    SemaphoreInternal* existing = (SemaphoreInternal*)ptrMap_Get(&s_sem_map, linux_sem);
    if (existing && existing->initialized == SEM_INIT_MAGIC) {
        ReleaseSRWLockExclusive(&s_sem_lock);
        return existing;
    }

    SemaphoreInternal* sem = (SemaphoreInternal*)calloc(1, sizeof(SemaphoreInternal));
    sem->handle = CreateSemaphoreA(NULL, (LONG)initial_value, SEM_VALUE_MAX, NULL);
    sem->value = (LONG)initial_value;
    sem->initialized = SEM_INIT_MAGIC;
    sem->refcount = 1;
    sem->name[0] = '\0';

    if (!sem->handle) {
        free(sem);
        ReleaseSRWLockExclusive(&s_sem_lock);
        return NULL;
    }

    ptrMap_Set(&s_sem_map, linux_sem, sem);

    ReleaseSRWLockExclusive(&s_sem_lock);
    return sem;
}

static SemaphoreInternal* FindSem(void* linux_sem) {
    AcquireSRWLockShared(&s_sem_lock);

    SemaphoreInternal* result = NULL;
    SemaphoreInternal* found = (SemaphoreInternal*)ptrMap_Get(&s_sem_map, linux_sem);
    if (found && found->initialized == SEM_INIT_MAGIC) {
        result = found;
    }

    ReleaseSRWLockShared(&s_sem_lock);
    return result;
}

static void DestroySem(void* linux_sem) {
    AcquireSRWLockExclusive(&s_sem_lock);

    SemaphoreInternal* s = (SemaphoreInternal*)ptrMap_Get(&s_sem_map, linux_sem);
    if (s) {
        if (s->handle) {
            CloseHandle(s->handle);
        }
        s->initialized = 0;
        free(s);
        ptrMap_Remove(&s_sem_map, linux_sem);
    }

    ReleaseSRWLockExclusive(&s_sem_lock);
}

// ============================================================
// Semaphore API - C Functions
// ============================================================

int emuSemInit(void* sem, int pshared, unsigned int value) {
    if (!sem) return -1;

    // pshared != 0 is not supported on Windows
    if (pshared != 0) {
        return -1;  // ENOSYS equivalent
    }

    SemaphoreInternal* s = GetOrCreateSem(sem, value);
    if (!s) return -1;

    return 0;
}

int emuSemDestroy(void* sem) {
    if (!sem) return -1;

    DestroySem(sem);
    return 0;
}

int emuSemWait(void* sem) {
    if (!sem) return -1;

    SemaphoreInternal* s = FindSem(sem);
    if (!s) {
        // Auto-initialize with value 0
        s = GetOrCreateSem(sem, 0);
        if (!s) return -1;
    }

    DWORD result = WaitForSingleObject(s->handle, INFINITE);
    if (result == WAIT_OBJECT_0) {
        InterlockedDecrement(&s->value);
        return 0;
    }

    return -1;
}

int emuSemTrywait(void* sem) {
    if (!sem) return -1;

    SemaphoreInternal* s = FindSem(sem);
    if (!s) return -1;

    DWORD result = WaitForSingleObject(s->handle, 0);
    if (result == WAIT_OBJECT_0) {
        InterlockedDecrement(&s->value);
        return 0;
    }

    // EAGAIN - would block
    return -1;
}

int emuSemTimedwait(void* sem, const struct timespec* abs_timeout) {
    if (!sem) return -1;

    SemaphoreInternal* s = FindSem(sem);
    if (!s) return -1;

    DWORD timeout_ms = INFINITE;
    if (abs_timeout) {
        timeout_ms = TimespecToMilliseconds(abs_timeout);
    }

    DWORD result = WaitForSingleObject(s->handle, timeout_ms);
    if (result == WAIT_OBJECT_0) {
        InterlockedDecrement(&s->value);
        return 0;
    }

    if (result == WAIT_TIMEOUT) {
        // ETIMEDOUT
        return -1;
    }

    return -1;
}

int emuSemPost(void* sem) {
    if (!sem) return -1;

    SemaphoreInternal* s = FindSem(sem);
    if (!s) return -1;

    InterlockedIncrement(&s->value);

    if (!ReleaseSemaphore(s->handle, 1, NULL)) {
        InterlockedDecrement(&s->value);
        return -1;
    }

    return 0;
}

int emuSemGetValue(void* sem, int* sval) {
    if (!sem || !sval) return -1;

    SemaphoreInternal* s = FindSem(sem);
    if (!s) return -1;

    *sval = (int)s->value;
    return 0;
}

void* emuSemOpen(const char* name, int oflag, int mode, unsigned int value) {
    (void)mode;
    if (!name) return (void*)-1;

    AcquireSRWLockExclusive(&s_sem_lock);
    EnsureSemMapsInit();

    char sem_name[256];
    strncpy(sem_name, name, 255);
    sem_name[255] = '\0';

    SemaphoreInternal* existing = (SemaphoreInternal*)strMap_Get(&s_named_sems, sem_name);
    if (existing) {
        // Linux O_CREAT=0x40, O_EXCL=0x80 (octal 0100, 0200)
        if ((oflag & 0x40) && (oflag & 0x80)) {
            ReleaseSRWLockExclusive(&s_sem_lock);
            // EEXIST
            return (void*)-1;
        }
        existing->refcount++;
        ReleaseSRWLockExclusive(&s_sem_lock);
        return (void*)existing;
    }

    if (!(oflag & 0x40)) { // !O_CREAT
        ReleaseSRWLockExclusive(&s_sem_lock);
        // ENOENT
        return (void*)-1;
    }

    // Windows name munging: semaphores are Global
    char win_name[512];
    strcpy(win_name, "Global\\");
    for (size_t i = 0; sem_name[i]; i++) {
        char c = sem_name[i];
        if (c == '/') continue;
        if (isalnum((unsigned char)c) || c == '_') {
            size_t len = strlen(win_name);
            if (len + 1 < sizeof(win_name)) {
                win_name[len] = c;
                win_name[len + 1] = '\0';
            }
        }
    }

    SemaphoreInternal* sem = (SemaphoreInternal*)calloc(1, sizeof(SemaphoreInternal));
    sem->handle = CreateSemaphoreA(NULL, (LONG)value, SEM_VALUE_MAX, win_name);
    sem->value = (LONG)value;
    sem->initialized = SEM_INIT_MAGIC;
    strncpy(sem->name, sem_name, 255);
    sem->name[255] = '\0';
    sem->refcount = 1;

    if (!sem->handle) {
        free(sem);
        ReleaseSRWLockExclusive(&s_sem_lock);
        return (void*)-1;
    }

    strMap_Set(&s_named_sems, sem_name, sem);
    ptrMap_Set(&s_sem_map, sem, sem); // Map to itself so emuSemWait works

    ReleaseSRWLockExclusive(&s_sem_lock);
    return (void*)sem;
}

int emuSemClose(void* sem) {
    if (!sem || sem == (void*)-1) return -1;

    AcquireSRWLockExclusive(&s_sem_lock);

    SemaphoreInternal* s = (SemaphoreInternal*)sem;
    if (s && s->initialized == SEM_INIT_MAGIC) {
        s->refcount--;
        if (s->refcount <= 0 && s->name[0] == '\0') {
            // Fully destroy if unlinked and no more references
            if (s->handle) CloseHandle(s->handle);
            ptrMap_Remove(&s_sem_map, s);
            free(s);
        }
    }

    ReleaseSRWLockExclusive(&s_sem_lock);
    return 0;
}

int emuSemUnlink(const char* name) {
    if (!name) return -1;

    AcquireSRWLockExclusive(&s_sem_lock);

    SemaphoreInternal* s = (SemaphoreInternal*)strMap_Get(&s_named_sems, name);
    if (s) {
        s->name[0] = '\0'; // Mark as unlinked
        strMap_Remove(&s_named_sems, name);

        if (s->refcount <= 0) {
            if (s->handle) CloseHandle(s->handle);
            ptrMap_Remove(&s_sem_map, s);
            free(s);
        }

        ReleaseSRWLockExclusive(&s_sem_lock);
        return 0;
    }

    ReleaseSRWLockExclusive(&s_sem_lock);
    return -1;
}

// ============================================================
// Cleanup function for semaphores
// ============================================================

/* Callback: close handle and free each SemaphoreInternal. */
static void CleanupSemEntry(void* key, void* value, void* userdata) {
    (void)key;
    (void)userdata;
    SemaphoreInternal* s = (SemaphoreInternal*)value;
    if (s) {
        if (s->handle) {
            CloseHandle(s->handle);
        }
        free(s);
    }
}

void SemaphoreCleanup(void) {
    AcquireSRWLockExclusive(&s_sem_lock);

    if (s_sem_maps_init) {
        ptrMap_ForEach(&s_sem_map, CleanupSemEntry, NULL);
        ptrMap_Clear(&s_sem_map);
    }

    ReleaseSRWLockExclusive(&s_sem_lock);
}
