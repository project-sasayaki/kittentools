#pragma once

#include <stdbool.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void initFpsLimiter(int _fpsNum, int _fpsDen, bool busyWait);
void fpsLimiter();
/* KITTEN_FRAMETIME_LOG=<file>: per-present timings. */
void frameTimeLog_beforeSwap(void);
void frameTimeLog_afterSwap(void);
