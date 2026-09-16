#include <winsock2.h>
#include <dirent.h>
#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h>
#include <direct.h>
#include <shlwapi.h>
#include "../config/config.h"
#include "../graphics/gpuVendor.h"
#include "../elfLoader/graphics.h"
#include "../log/log.h"
#include "file.h"
#include "network.h"
#include "../devices/device.h"

#define REAL_FUNC(name) name
#define HOOK_FILE_NAME "NUL"

static int LinuxFlagsToWindowsFlags(int flags)
{
    int winFlags = 0x8000; // _O_BINARY

    //
    // access mode
    //
    switch (flags & 0x3)
    {
        case 0x0:
            winFlags |= 0x0000; // _O_RDONLY
            break;

        case 0x1:
            winFlags |= 0x0001; // _O_WRONLY
            break;

        case 0x2:
            winFlags |= 0x0002; // _O_RDWR
            break;

        default:
            winFlags |= 0x0000;
            break;
    }

    //
    // O_CREAT
    //
    if (flags & 0x40)
        winFlags |= 0x0100;

    //
    // O_EXCL
    //
    if (flags & 0x80)
        winFlags |= 0x0400;

    //
    // O_TRUNC
    //
    if (flags & 0x200)
        winFlags |= 0x0200;

    //
    // O_APPEND
    //
    if (flags & 0x400)
        winFlags |= 0x0008;

    return winFlags;
}

void getKittenPath(const char *originalPath, char *targetPath)
{
    char targetDir[512];
    if (strncmp(originalPath, "./", 2) == 0) {
        snprintf(targetPath, 512, "./kitten/%s", originalPath + 2);
    } else if (originalPath[0] == '/') {
        snprintf(targetPath, 512, "kitten/%s", originalPath + 1);
    }
    else {
        snprintf(targetPath, 512, "kitten/%s", originalPath);
    }
    
    strcpy(targetDir, targetPath);
    char *lastSlash = strrchr(targetDir, '/');
    if (lastSlash != NULL) {
        *lastSlash = '\0'; 
        
        char tempDir[512];
        char *pos = targetDir;
        bool isAbsolutePath = (targetDir[0] == '/');
        
        if (isAbsolutePath) {
            strcpy(tempDir, "/");
            pos++;
        } else {
            strcpy(tempDir, "");
        }
        
        char *slash;
         
        while ((slash = strchr(pos, '/')) != NULL) {
            *slash = '\0';
            
            if (strlen(tempDir) > 0 && tempDir[strlen(tempDir)-1] != '/') {
                strcat(tempDir, "/");
            }
            strcat(tempDir, pos);           
            _mkdir(tempDir);
            
            *slash = '/';
            pos = slash + 1;
        }
        
        _mkdir(targetDir);
    }     
}

int hook_mkdir(const char *path, mode_t mode)
{
    if (strncmp(path, "/tmp", 4) == 0)
    {
        path += 1;
    }

    char winPath[MAX_PATH];
    getKittenPath(path, winPath);
    return _mkdir(winPath);
}

int hook_open(const char *pathname, int flags, ...)
{
    int fd = 0;
    int mode = 0; 
    char *path = (char *)pathname;
    char winPath[MAX_PATH];
    DeviceDescriptor* device = NULL;
    if (flags & 0x40)
    {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }

    int (*_open)(const char *pathname, int flags, ...) = REAL_FUNC(open);

    device = getDeviceByPath((char*)pathname);
    if (device)
    {
        fd = _open(device->redirectPoint, flags, mode);
        device->fd = fd;
        return fd;
    }

    flags = LinuxFlagsToWindowsFlags(flags);

    if (strncmp(pathname, "/tmp/", 5) == 0)
    {
        getKittenPath(pathname, winPath);
        path = winPath;
    }
    
    if (mode)
        return _open(path, flags, mode);
    return _open(path, flags);
}

int hook_close(int fd)
{
    int (*_close)(int fd) = REAL_FUNC(close);

    return _close(fd);
}

int hook_open64(const char *pathname, int flags, ...)
{
    va_list args;
    va_start(args, flags);
    int mode = va_arg(args, int);
    va_end(args);

    return hook_open(pathname, flags, mode);
}

FILE *hook_fopen(const char *restrict pathname, const char *restrict mode)
{
    void *address = __builtin_return_address(0);
    char newPath[MAX_PATH];
    char *path = (char *)pathname;
    uint8_t gpu_id = getGPUVendorID();
    FILE *(*_fopen)(const char *restrict pathname, const char *restrict mode) = REAL_FUNC(fopen);
    
    static BOOL warning = false;

    if (!strcmp(pathname, "/var/tmp/warning"))
    {
        if (warning)
            return _fopen("NUL", mode);
        warning = true;
        return 0;
    }

    /* On non-NVIDIA GPUs, reject cached NVIDIA assembly so the game asks Cg
     * to compile the original source for the portable ARB profiles. */
    if (gpu_id != NVIDIA_GPU)
    {
        if ((strstr(pathname, "asm_lbg") != NULL) || (strstr(pathname, "asm_gl") != NULL))
        {
            /* The game will have to recompile from Cg source (same refusal as
             * LinuxLoader's sharedFopen). */
            return 0;
        }

    }
    
    if (strncmp(pathname, "/tmp/", 5) == 0 || strncmp(pathname, "/var/tmp/", 9) == 0)
    {
        getKittenPath(pathname, newPath);
        path = newPath;
    }

    log_debug(plfHooks, "fopen: %s (as %s) with mode %s at %p", pathname, path, mode, address);
    return _fopen(path, mode);
}

FILE *hook_fopen64(const char *pathname, const char *mode)
{
    FILE *(*_fopen64)(const char *restrict pathname, const char *restrict mode) = REAL_FUNC(fopen64);
    return _fopen64(pathname, mode);
}

int hook_fclose(FILE *stream)
{
    int (*_fclose)(FILE *stream) = REAL_FUNC(fclose);
    return _fclose(stream);
}

char *hook_fgets(char *str, int n, FILE *stream)
{
    char *(*_fgets)(char *str, int n, FILE *stream) = REAL_FUNC(fgets);

    return _fgets(str, n, stream);
}

ssize_t hook_read(int fd, void *buf, size_t count)
{
    int (*_read)(int fd, void *buf, size_t count) = REAL_FUNC(read);
    DeviceDescriptor *device = getDevice(fd);

    if (device && device->read)
        return device->read(fd, buf, count);

    return _read(fd, buf, count);
}

size_t hook_fread(void *buf, size_t size, size_t count, FILE *stream)
{
    size_t (*_fread)(void *buf, size_t size, size_t count, FILE *stream) = REAL_FUNC(fread);

    return _fread(buf, size, count, stream);
}

long int hook_ftell(FILE *stream)
{
    long int (*_ftell)(FILE *stream) = REAL_FUNC(ftell);

    return _ftell(stream);
}

int hook_fseek(FILE *stream, long int offset, int whence)
{
    int (*_fseek)(FILE *stream, long int offset, int whence) = REAL_FUNC(fseek);

    return _fseek(stream, offset, whence);
}

void hook_rewind(FILE *stream)
{
    void (*_rewind)(FILE *stream) = REAL_FUNC(rewind);
    _rewind(stream);
}

ssize_t hook_write(int fd, const void *buf, size_t count)
{
    int (*_write)(int fd, const void *buf, size_t count) = REAL_FUNC(write);

    DeviceDescriptor *device = getDevice(fd);
    
    if (device && device->write)
        return device->write(fd, buf, count);

    return _write(fd, buf, count);
}

int hook_ioctl(int fd, unsigned long int request, ...)
{
    va_list args;
    va_start(args, request);
    void *argp = va_arg(args, void *);
    va_end(args);

    DeviceDescriptor *device = getDevice(fd);

    // system device ioctl
    if (device && device->ioctl)
        device->ioctl(fd, request, argp);

    if (request == SIOCGIFFLAGS || request == SIOCGIFADDR || request == SIOCGIFNETMASK)
        return handleNetworkIoctl(fd, request, argp);
    return 0;
}
