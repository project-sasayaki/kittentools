// ============================================================
// pthread_once.c - Once control implementation
// ============================================================

#include "pthreadEmu.h"
#include "pthreadInternal.h"

// ============================================================
// pthread_once Implementation
// ============================================================

// Linux pthread_once_t is just an int (4 bytes)
// Values: 0 = not done, 1 = in progress, 2 = done

#define ONCE_STATE_INIT     0
#define ONCE_STATE_RUNNING  1
#define ONCE_STATE_DONE     2

int pthreadEmu_pthreadOnce(void* once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine) return LINUX_EINVAL;

    volatile LONG* state = (volatile LONG*)once_control;

    // Fast path: already done
    if (*state == ONCE_STATE_DONE) {
        return 0;
    }

    // Try to become the initializer
    LONG old = InterlockedCompareExchange(state, ONCE_STATE_RUNNING, ONCE_STATE_INIT);

    if (old == ONCE_STATE_INIT) {
        // We won the race - run the initializer
        init_routine();

        // Mark as done
        InterlockedExchange(state, ONCE_STATE_DONE);

        // Memory barrier to ensure visibility
        MemoryBarrier();
        return 0;
    }

    if (old == ONCE_STATE_DONE) {
        // Already done by another thread
        return 0;
    }

    // Someone else is running the initializer - wait for them
    // Use exponential backoff to reduce contention
    int spin_count = 0;
    while (*state == ONCE_STATE_RUNNING) {
        if (spin_count < 20) {
            // Spin for a bit first
            YieldProcessor();
            spin_count++;
        }
        else {
            // Then sleep
            Sleep(0);
        }
    }

    return 0;
}
