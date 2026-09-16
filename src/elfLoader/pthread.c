#include "pthread.h"
#include "pthread/pthreadEmu.h"
#include "symbol.h"
#include "libc.h"
#include "../log/log.h"
#include "elfLoader.h"

int hook_pthreadOnce(int *once_control, void (*init_routine)(void));
int hook_schedGetPriorityMax(int policy);
int hook_schedGetPriorityMin(int policy);

void pthreadBridge_initBridges(void)
{
    log_debug(plfBridges, "Initializing Pthread Emulation...");

    PthreadEmu_Initialize();

    /* Thread functions */
    symbol_register("pthread_create", pthreadEmu_pthreadCreate, NULL);
    symbol_register("pthread_join", pthreadEmu_pthreadJoin, NULL); 
    symbol_register("pthread_detach", pthreadEmu_pthreadDetach, NULL);
    symbol_register("pthread_exit", pthreadEmu_pthreadExit, NULL);
    symbol_register("pthread_self", pthreadEmu_pthreadSelf, NULL);
    symbol_register("pthread_equal", pthreadEmu_pthreadEqual, NULL);
    symbol_register("pthread_cancel", pthreadEmu_pthreadCancel, NULL);

    /* Thread attributes */
    symbol_register("pthread_attr_init", pthreadEmu_pthreadAttrInit, NULL);
    symbol_register("pthread_attr_destroy", pthreadEmu_pthreadAttrDestroy, NULL);
    symbol_register("pthread_attr_setstacksize", pthreadEmu_pthreadAttrSetstacksize, NULL);
    symbol_register("pthread_attr_getstacksize", pthreadEmu_pthreadAttrGetstacksize, NULL);
    symbol_register("pthread_attr_setdetachstate", pthreadEmu_pthreadAttrSetdetachstate, NULL);
    symbol_register("pthread_attr_getdetachstate", pthreadEmu_pthreadAttrGetdetachstate, NULL);

    /* Scheduling */
    symbol_register("pthread_setschedparam", pthreadEmu_pthreadSetSchedparam, NULL);
    symbol_register("pthread_getschedparam", pthreadEmu_pthreadGetSchedparam, NULL);
    symbol_register("sched_yield", pthreadEmu_schedYield, NULL);
    symbol_register("pthread_attr_setschedparam", hook_ret_zero, NULL);
    symbol_register("pthread_attr_setschedpolicy", hook_ret_zero, NULL);
    symbol_register("sched_getaffinity", hook_ret_zero, NULL);
    symbol_register("sched_setaffinity", hook_ret_zero, NULL);

    /* Mutex functions */
    symbol_register("pthread_mutex_init", pthreadEmu_pthreadMutexInit, NULL);
    symbol_register("pthread_mutex_destroy", pthreadEmu_pthreadMutexDestroy, NULL);
    symbol_register("pthread_mutex_lock", pthreadEmu_pthreadMutexLock, NULL);
    symbol_register("pthread_mutex_trylock", pthreadEmu_pthreadMutexTrylock, NULL);
    symbol_register("pthread_mutex_timedlock", pthreadEmu_pthreadMutexTimedlock, NULL);
    symbol_register("pthread_mutex_unlock", pthreadEmu_pthreadMutexUnlock, NULL);

    /* Mutex attributes */
    symbol_register("pthread_mutexattr_init", pthreadEmu_pthreadMutexattrInit, NULL);
    symbol_register("pthread_mutexattr_destroy", pthreadEmu_pthreadMutexattrDestroy, NULL);
    symbol_register("pthread_mutexattr_settype", pthreadEmu_pthreadMutexattrSettype, NULL);
    symbol_register("pthread_mutexattr_gettype", pthreadEmu_pthreadMutexattrGettype, NULL);

    /* Condition variable functions */
    symbol_register("pthread_cond_init", pthreadEmu_pthreadCondInit, NULL);
    symbol_register("pthread_cond_destroy", pthreadEmu_pthreadCondDestroy, NULL);
    symbol_register("pthread_cond_wait", pthreadEmu_pthreadCondWait, NULL);
    symbol_register("pthread_cond_timedwait", pthreadEmu_pthreadCondTimedwait, NULL);
    symbol_register("pthread_cond_signal", pthreadEmu_pthreadCondSignal, NULL);
    symbol_register("pthread_cond_broadcast", pthreadEmu_pthreadCondBroadcast, NULL);

    /* Condition variable attributes */
    symbol_register("pthread_condattr_init", pthreadEmu_pthreadCondattrInit, NULL);
    symbol_register("pthread_condattr_destroy", pthreadEmu_pthreadCondattrDestroy, NULL);

    /* Read-write lock functions */
    symbol_register("pthread_rwlock_init", pthreadEmu_pthreadRwlockInit, NULL);
    symbol_register("pthread_rwlock_destroy", pthreadEmu_pthreadRwlockDestroy, NULL);
    symbol_register("pthread_rwlock_rdlock", pthreadEmu_pthreadRwlockRdlock, NULL);
    symbol_register("pthread_rwlock_tryrdlock", pthreadEmu_pthreadRwlockTryrdlock, NULL);
    symbol_register("pthread_rwlock_wrlock", pthreadEmu_pthreadRwlockWrlock, NULL);
    symbol_register("pthread_rwlock_trywrlock", pthreadEmu_pthreadRwlockTrywrlock, NULL);  
    symbol_register("pthread_rwlock_unlock", pthreadEmu_pthreadRwlockUnlock, NULL);

    /* Once control */
    symbol_register("pthread_once", pthreadEmu_pthreadOnce, NULL);

    /* TLS functions */
    symbol_register("pthread_key_create", pthreadEmu_pthreadKeyCreate, NULL);
    symbol_register("pthread_key_delete", pthreadEmu_pthreadKeyDelete, NULL);
    symbol_register("pthread_setspecific", pthreadEmu_pthreadSetSpecific, NULL);
    symbol_register("pthread_getspecific", pthreadEmu_pthreadGetSpecific, NULL);

    /* Barrier functions */
    symbol_register("pthread_barrier_init", pthreadEmu_pthreadBarrierInit, NULL);
    symbol_register("pthread_barrier_destroy", pthreadEmu_pthreadBarrierDestroy, NULL);
    symbol_register("pthread_barrier_wait", pthreadEmu_pthreadBarrierWait, NULL);

    /* Spinlock functions */
    symbol_register("pthread_spin_init", pthreadEmu_pthreadSpinInit, NULL);
    symbol_register("pthread_spin_destroy", pthreadEmu_pthreadSpinDestroy, NULL);
    symbol_register("pthread_spin_lock", pthreadEmu_pthreadSpinLock, NULL);
    symbol_register("pthread_spin_trylock", pthreadEmu_pthreadSpinTrylock, NULL);
    symbol_register("pthread_spin_unlock", pthreadEmu_pthreadSpinUnlock, NULL);

    /* Semaphores */
    symbol_register("sem_init", emuSemInit, NULL);
    symbol_register("sem_destroy", emuSemDestroy, NULL);
    symbol_register("sem_wait", emuSemWait, NULL);
    symbol_register("sem_trywait", emuSemTrywait, NULL);
    symbol_register("sem_post", emuSemPost, NULL);
    symbol_register("sem_getvalue", emuSemGetValue, NULL);
    symbol_register("sem_open", emuSemOpen, NULL);
    symbol_register("sem_close", emuSemClose, NULL);
    symbol_register("sem_unlink", emuSemUnlink, NULL);

    symbol_register("pthread_once", hook_pthreadOnce, NULL);

    symbol_register("sched_get_priority_max", hook_schedGetPriorityMax, NULL);
    symbol_register("sched_get_priority_min", hook_schedGetPriorityMin, NULL);
}

int hook_pthreadOnce(int *once_control, void (*init_routine)(void))
{
    if (once_control && *once_control == 0)
    {
        *once_control = 1; /* Running */
        if (init_routine)
        {
            init_routine();
        }
        *once_control = 2; /* Completed */
    }
    return 0; /* Success */
}

int hook_schedGetPriorityMax(int policy)
{
    return 99;
}

int hook_schedGetPriorityMin(int policy)
{
    return 0;
}