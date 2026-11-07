#pragma once

#include <stddef.h>
#include <sys/types.h>

int namcoJvioInit(void);
ssize_t namcoJvioRead(int fd, void *buf, size_t count);
ssize_t namcoJvioWrite(int fd, const void *buf, size_t count);
int namcoJvioIoctl(int fd, unsigned long request, void *data);
int namcoJvioClose(int fd);
