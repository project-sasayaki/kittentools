#include <winsock2.h>
#include <windows.h> 


int initBaseboard();

ssize_t baseboardRead(int fd, void *buf, size_t count);
ssize_t baseboardWrite(int fd, const void *buf, size_t count);

int baseboardIoctl(int fd, unsigned long request, void *data);
