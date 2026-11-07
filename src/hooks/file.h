#pragma once

#include <winsock2.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
typedef enum
{
    NO_DEVICE = 0,
    BASEBOARD = 1,
    EEPROM = 2,
    SERIAL0 = 3,
    SERIAL1 = 4,
    PCI_CARD_000 = 5
} DeviceType;

typedef enum
{
    CPUINFO = 0,
    OSRELEASE = 1,
    PCI_CARD_1F0 = 2,  
    ROUTE = 3
} FileTypes;

#ifdef __cplusplus
extern "C" {
#endif

int hook_mkdir(const char *path, mode_t mode);
int hook_open(const char *pathname, int flags, ...);
int hook_open64(const char *pathname, int flags, ...);
int hook_write(int fd, const void *buf, size_t count);
FILE *hook_fopen(const char *pathname, const char *mode);
FILE *hook_fopen64(const char *pathname, const char *mode);
int hook_fclose(FILE *stream);
int hook_close(int fd);
char *hook_fgets(char *str, int n, FILE *stream);
ssize_t hook_read(int fd, void *buf, size_t count);
size_t hook_fread(void *buf, size_t size, size_t count, FILE *stream);
long int hook_ftell(FILE *stream);
int hook_fseek(FILE *stream, long int offset, int whence);
void hook_rewind(FILE *stream);
int hook_ioctl(int fd, unsigned long int request, ...);
#define KITTEN_PATH_MAX 512
void getKittenPath(const char *originalPath, char *targetPath);
const char *mapGuestTmpPath(const char *path, char buffer[KITTEN_PATH_MAX]);
FILE *hook_fdopen(int fd, const char *mode);

void file_use_linux_binary_semantics(bool enable);
void file_redirect_path(const char *guestPath, const char *hostPath);

typedef struct
{
    void (*opened)(const char *guestPath, int fd, FILE *stream);
    void (*read)(int fd, FILE *stream, long long offset, size_t bytes);
    void (*closed)(int fd, FILE *stream);
} FileObserver;
void file_set_observer(const FileObserver *observer);

#ifdef __cplusplus
}
#endif