#pragma once

/* rand(), srand(), random() and srandom() as glibc has them: one 31-bit generator for the whole
 * process, whichever thread seeds it or draws from it. The host's rand() is 15 bits wide and keeps
 * a separate sequence and seed in every thread. */
int glibcRandom_rand(void);
void glibcRandom_srand(unsigned int seed);
long glibcRandom_random(void);
void glibcRandom_srandom(unsigned int seed);

/* glibc's rand_r(): 31 bits from the caller's own seed. */
int glibcRandom_rand_r(unsigned int *seed);
