/* glibc's default generator (random_r.c, TYPE_3): r[i] = r[i - 3] + r[i - 31], returned without its lowest bit. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

#include "glibcRandom.h"

#define DEGREE 31
#define SEPARATION 3
#define DISCARDED (DEGREE * 10)
#define DEFAULT_SEED 1

static SRWLOCK g_lock = SRWLOCK_INIT;
static uint32_t g_state[DEGREE];
static int g_front, g_rear;
static bool g_seeded;

static long next(void)
{
    uint32_t value = g_state[g_front] += g_state[g_rear];
    if (++g_front >= DEGREE)
        g_front = 0;
    if (++g_rear >= DEGREE)
        g_rear = 0;
    return (long)(value >> 1);
}

static void seed_state(unsigned int seed)
{
    if (seed == 0)
        seed = 1;

    int32_t word = (int32_t)seed;
    g_state[0] = seed;
    for (int i = 1; i < DEGREE; i++)
    {
        /* 16807 * word mod 2147483647, without overflow */
        int32_t high = word / 127773, low = word % 127773;
        word = 16807 * low - 2836 * high;
        if (word < 0)
            word += 2147483647;
        g_state[i] = (uint32_t)word;
    }
    g_front = SEPARATION;
    g_rear = 0;
    for (int i = 0; i < DISCARDED; i++)
        next();
    g_seeded = true;
}

long glibcRandom_random(void)
{
    AcquireSRWLockExclusive(&g_lock);
    if (!g_seeded)
        seed_state(DEFAULT_SEED);
    long value = next();
    ReleaseSRWLockExclusive(&g_lock);
    return value;
}

void glibcRandom_srandom(unsigned int seed)
{
    AcquireSRWLockExclusive(&g_lock);
    seed_state(seed);
    ReleaseSRWLockExclusive(&g_lock);
}

int glibcRandom_rand(void)
{
    return (int)glibcRandom_random();
}

void glibcRandom_srand(unsigned int seed)
{
    glibcRandom_srandom(seed);
}

/* rand_r.c: three steps of a linear congruential generator, 11 + 10 + 10 bits. */
int glibcRandom_rand_r(unsigned int *seed)
{
    unsigned int next = *seed;

    next = next * 1103515245u + 12345u;
    int result = (int)(next / 65536u % 2048u);
    next = next * 1103515245u + 12345u;
    result = (result << 10) ^ (int)(next / 65536u % 1024u);
    next = next * 1103515245u + 12345u;
    result = (result << 10) ^ (int)(next / 65536u % 1024u);

    *seed = next;
    return result;
}
