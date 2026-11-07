#pragma once

#include <stddef.h>
#include <sys/types.h>

int namcoCardReaderInit(void);
ssize_t namcoCardReaderRead(int fd, void *buf, size_t count);
ssize_t namcoCardReaderWrite(int fd, const void *buf, size_t count);
int namcoCardReaderIoctl(int fd, unsigned long request, void *data);
int namcoCardReaderClose(int fd);
void namcoCardReaderRequestInsert(void);
