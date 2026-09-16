#pragma once

#include <stdint.h>
#include <stddef.h>

/* Forward declaration */
struct timespec;

#include "pthreadInternal.h"

/* Init/Shutdown */
void PthreadEmu_Initialize(void);
void PthreadEmu_Shutdown(void);

/* -------------------------------------------------------- */
/*  Thread API                                              */
/* -------------------------------------------------------- */
int      pthreadEmu_pthreadCreate(void *thread, const void *attr,
                                  void *(*start_routine)(void *), void *arg);
int      pthreadEmu_pthreadJoin(uint32_t thread, void **retval);
int      pthreadEmu_pthreadDetach(uint32_t thread);
void     pthreadEmu_pthreadExit(void *retval);
uint32_t pthreadEmu_pthreadSelf(void);
int      pthreadEmu_pthreadEqual(uint32_t t1, uint32_t t2);

/* Thread cancel (limited support) */
int      pthreadEmu_pthreadCancel(uint32_t thread);
int      pthreadEmu_pthreadSetcancelstate(int state, int *oldstate);
int      pthreadEmu_pthreadSetcanceltype(int type, int *oldtype);
void     pthreadEmu_pthreadTestcancel(void);

/* Thread attributes */
int      pthreadEmu_pthreadAttrInit(void *attr);
int      pthreadEmu_pthreadAttrDestroy(void *attr);
int      pthreadEmu_pthreadAttrSetdetachstate(void *attr, int detachstate);
int      pthreadEmu_pthreadAttrGetdetachstate(const void *attr, int *detachstate);
int      pthreadEmu_pthreadAttrSetstacksize(void *attr, size_t stacksize);
int      pthreadEmu_pthreadAttrGetstacksize(const void *attr, size_t *stacksize);

/* -------------------------------------------------------- */
/*  Mutex API                                               */
/* -------------------------------------------------------- */
int      pthreadEmu_pthreadMutexInit(void *mutex, const void *attr);
int      pthreadEmu_pthreadMutexDestroy(void *mutex);
int      pthreadEmu_pthreadMutexLock(void *mutex);
int      pthreadEmu_pthreadMutexTrylock(void *mutex);
int      pthreadEmu_pthreadMutexTimedlock(void *mutex, const struct timespec *abstime);
int      pthreadEmu_pthreadMutexUnlock(void *mutex);

/* Mutex attributes */
int      pthreadEmu_pthreadMutexattrInit(void *attr);
int      pthreadEmu_pthreadMutexattrDestroy(void *attr);
int      pthreadEmu_pthreadMutexattrSettype(void *attr, int type);
int      pthreadEmu_pthreadMutexattrGettype(const void *attr, int *type);

/* -------------------------------------------------------- */
/*  Condition Variable API                                  */
/* -------------------------------------------------------- */
int      pthreadEmu_pthreadCondInit(void *cond, const void *attr);
int      pthreadEmu_pthreadCondDestroy(void *cond);
int      pthreadEmu_pthreadCondWait(void *cond, void *mutex);
int      pthreadEmu_pthreadCondTimedwait(void *cond, void *mutex, const struct timespec *abstime);
int      pthreadEmu_pthreadCondSignal(void *cond);
int      pthreadEmu_pthreadCondBroadcast(void *cond);

/* Cond attributes */
int      pthreadEmu_pthreadCondattrInit(void *attr);
int      pthreadEmu_pthreadCondattrDestroy(void *attr);

/* -------------------------------------------------------- */
/*  Read-Write Lock API                                     */
/* -------------------------------------------------------- */
int      pthreadEmu_pthreadRwlockInit(void *rwlock, const void *attr);
int      pthreadEmu_pthreadRwlockDestroy(void *rwlock);
int      pthreadEmu_pthreadRwlockRdlock(void *rwlock);
int      pthreadEmu_pthreadRwlockTryrdlock(void *rwlock);
int      pthreadEmu_pthreadRwlockWrlock(void *rwlock);
int      pthreadEmu_pthreadRwlockTrywrlock(void *rwlock);
int      pthreadEmu_pthreadRwlockUnlock(void *rwlock);

/* -------------------------------------------------------- */
/*  Barrier API                                             */
/* -------------------------------------------------------- */
int      pthreadEmu_pthreadBarrierInit(void *barrier, const void *attr, unsigned int count);
int      pthreadEmu_pthreadBarrierDestroy(void *barrier);
int      pthreadEmu_pthreadBarrierWait(void *barrier);

/* -------------------------------------------------------- */
/*  Spinlock API                                            */
/* -------------------------------------------------------- */
int      pthreadEmu_pthreadSpinInit(void *lock, int pshared);
int      pthreadEmu_pthreadSpinDestroy(void *lock);
int      pthreadEmu_pthreadSpinLock(void *lock);
int      pthreadEmu_pthreadSpinTrylock(void *lock);
int      pthreadEmu_pthreadSpinUnlock(void *lock);

/* -------------------------------------------------------- */
/*  Once API                                                */
/* -------------------------------------------------------- */
int      pthreadEmu_pthreadOnce(void *once_control, void (*init_routine)(void));

/* -------------------------------------------------------- */
/*  TLS API                                                 */
/* -------------------------------------------------------- */
int      pthreadEmu_pthreadKeyCreate(void *key, void (*destructor)(void *));
int      pthreadEmu_pthreadKeyDelete(uint32_t key);
int      pthreadEmu_pthreadSetSpecific(uint32_t key, const void *value);
void    *pthreadEmu_pthreadGetSpecific(uint32_t key);

/* -------------------------------------------------------- */
/*  Scheduling API                                          */
/* -------------------------------------------------------- */
int      pthreadEmu_pthreadSetSchedparam(uint32_t thread, int policy, const void *param);
int      pthreadEmu_pthreadGetSchedparam(uint32_t thread, int *policy, void *param);
int      pthreadEmu_schedYield(void);

/* -------------------------------------------------------- */
/*  Semaphore API                                           */
/* -------------------------------------------------------- */
int      emuSemInit(void *sem, int pshared, unsigned int value);
int      emuSemDestroy(void *sem);
int      emuSemWait(void *sem);
int      emuSemTrywait(void *sem);
int      emuSemTimedwait(void *sem, const struct timespec *abs_timeout);
int      emuSemPost(void *sem);
int      emuSemGetValue(void *sem, int *sval);
void    *emuSemOpen(const char *name, int oflag, int mode, unsigned int value);
int      emuSemClose(void *sem);
int      emuSemUnlink(const char *name);

/* Cleanup function for semaphores (called by PthreadEmu_Shutdown) */
void     SemaphoreCleanup(void);