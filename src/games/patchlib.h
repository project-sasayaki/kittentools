#pragma once

#include <stdio.h>

void setVariable(size_t address, size_t value);
void patchMachineCode(size_t address, char *value);
int patchMemory(void *address, const void *data, size_t size);
void stubReturn();
int stubRetZero();
int stubRetOne();
int stubRetThree();
int stubRetMinusOne();