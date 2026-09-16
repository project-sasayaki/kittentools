#include <sys/types.h>

int initEeprom();
int eepromIoctl(int fd, unsigned long request, void *data);