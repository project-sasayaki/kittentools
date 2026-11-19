#pragma once

#include <stddef.h>
#include <sys/types.h>

struct linux_termios;

/* path: "COM3", or a named pipe ("\\.\pipe\name") another program has created. One line. */
void serialLinkConfigure(const char *path, const char *name);

int serialLinkInit(void);
ssize_t serialLinkRead(int fd, void *buf, size_t count);
ssize_t serialLinkWrite(int fd, const void *buf, size_t count);
int serialLinkIoctl(int fd, unsigned long request, void *data);
int serialLinkSetAttr(int fd, const struct linux_termios *termios);
int serialLinkClose(int fd);
