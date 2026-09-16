#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <io.h>
#include <direct.h>
#include <windows.h>
#include "../hooks/file.h"
#include "elfLoader.h"
#include "file.h"
#include "libc.h"
#include "symbol.h"
#include "../log/log.h"
#include "../config/config.h"

size_t hook_fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
size_t hook_writev(int fd, const struct iovec *iov, int iovcnt);
int hook_creat(const char *pathname, int mode);
char *hook_getcwd(char *buf, size_t size);

int hook_fputc(int c, FILE *stream);
void *hook_opendir(const char *name);
struct linux_dirent *hook_readdir(void *dirp);
int hook_closedir(void *dirp);

int hook_unlink(const char *pathname);
int hook_remove(const char *pathname);

int hook_xstat(int ver, const char *path, struct linux_stat *buf);
int hook_fxstat(int ver, int fd, struct linux_stat *buf);
int hook_fxstat64(int ver, int fd, struct linux_stat64 *buf);
int hook_xmknod(int ver, const char *path, unsigned int mode, void *dev);
int hook_fcntl(int fd, int cmd, ...);


void filesystemBridge_initBridges(void)
{
    log_debug(plfBridges, "Initializing FileSystemBridge...");

    symbol_register("fopen", hook_fopen, NULL);
    symbol_register("fread", hook_fread, NULL);
    symbol_register("fwrite", hook_fwrite, NULL);
    symbol_register("fseek", hook_fseek, NULL);
    symbol_register("ftell", hook_ftell, NULL);
    symbol_register("fclose", hook_fclose, NULL);
    symbol_register("ferror", ferror, NULL);
    symbol_register("rewind", rewind, NULL);
    symbol_register("feof", feof, NULL);
    symbol_register("fgets", fgets, NULL);
    symbol_register("fgetc", fgetc, NULL);
    symbol_register("fflush", fflush, NULL);
    symbol_register("fputs", fputs, NULL);
    symbol_register("fputc", hook_fputc, NULL);
    symbol_register("getc", fgetc, NULL);
    symbol_register("ungetc", ungetc, NULL);

    symbol_register("fopen64", hook_fopen, NULL);
    symbol_register("fseeko64", hook_fseek, NULL);
    symbol_register("lseek64", _lseeki64, NULL);
    symbol_register("ftello64", hook_ftell, NULL);

    symbol_register("fileno", fileno, NULL);

    symbol_register("read", hook_read, NULL);
    symbol_register("write", hook_write, NULL);
    symbol_register("close", hook_close, NULL);
    symbol_register("open", hook_open, NULL);
    symbol_register("fdopen", fdopen, NULL);
    symbol_register("setvbuf", setvbuf, NULL);
    symbol_register("writev", hook_writev, NULL);

    symbol_register("ioctl", hook_ioctl, NULL);
    symbol_register("creat", hook_creat, NULL);
    symbol_register("getcwd", hook_getcwd, NULL);
    symbol_register("mkdir", hook_mkdir, NULL);
    symbol_register("opendir", hook_opendir, NULL);
    symbol_register("readdir", hook_readdir, NULL);
    symbol_register("closedir", hook_closedir, NULL);

    symbol_register("unlink", hook_unlink, NULL);
    symbol_register("remove", hook_remove, NULL);

    symbol_register("__xstat", hook_xstat, NULL);
    symbol_register("__lxstat", hook_xstat, NULL);
    symbol_register("__fxstat", hook_fxstat, NULL); 
    symbol_register("__fxstat64", hook_fxstat64, NULL);
    symbol_register("__xmknod", hook_xmknod, NULL);
    symbol_register("fcntl", hook_fcntl, NULL);
}

size_t hook_fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    if (stream == stderr || stream == stdout)
    {
        log_game(plfBridges, "%s", ptr);
        return 0;
    }

    return fwrite(ptr, size, count, stream);
}

size_t hook_writev(int fd, const struct iovec *iov, int iovcnt)
{
    size_t total_written = 0;
    for (int i = 0; i < iovcnt; ++i) {
        int written = _write(fd, iov[i].iov_base, iov[i].iov_len);
        if (written < 0) {
            if (total_written == 0) return -1;
            break;
        }
        total_written += written;
        if ((size_t)written < iov[i].iov_len) break;
    }
    return total_written;
}

int hook_creat(const char *pathname, int mode)
{
    if (strncmp(pathname, "/tmp", 4) == 0)
        pathname += 1;

    if (strncmp(pathname, "/var/tmp", 8) == 0)
        pathname += 5;

    char winPath[MAX_PATH];
    getKittenPath(pathname, winPath);
    return _creat(winPath, mode);
}

char *hook_getcwd(char *buf, size_t size)
{
    if (buf != NULL)
        return _getcwd(buf, (int)size);
    char *tmp = _getcwd(NULL, 0);
    if (!tmp)
        return NULL;
    size_t len = strlen(tmp) + 1;
    if (size > 0 && size < len)
        len = size;
    char *aligned_ptr = (char *)_aligned_malloc(len, 16);
    if (aligned_ptr)
    {
        strncpy(aligned_ptr, tmp, len);
        aligned_ptr[len - 1] = '\0';
    }
    _aligned_free(tmp);
    return aligned_ptr;
}

void *hook_opendir(const char *name)
{
    char newPath[MAX_PATH];
    const char *path = name;

    if (name[0] == '/')
    {
        getKittenPath(name, newPath);
        path = newPath;
    }

    char searchPath[MAX_PATH];
    strncpy(searchPath, path, MAX_PATH - 1);
    searchPath[MAX_PATH - 1] = '\0';

    if (searchPath[0] == '\0')
        return NULL;

    if (searchPath[strlen(searchPath) - 1] == '/')
    {
        strncat(searchPath, "*", MAX_PATH - strlen(searchPath) - 1);
    }
    else
    {
        strncat(searchPath, "/*", MAX_PATH - strlen(searchPath) - 1);
    }

    DIR_Impl *dir = (DIR_Impl *)calloc(1, sizeof(DIR_Impl));
    strncpy(dir->path, path, MAX_PATH - 1);
    dir->path[MAX_PATH - 1] = '\0';
    dir->hFind = FindFirstFileA(searchPath, &dir->findData);

    if (dir->hFind == INVALID_HANDLE_VALUE)
    {
        free(dir);
        return NULL;
    }

    dir->first_read = true;
    dir->finished = false;
    return (void *)dir;
}

struct linux_dirent *hook_readdir(void *dirp)
{

    if (!dirp)
        return NULL;
    DIR_Impl *dir = (DIR_Impl *)dirp;

    if (dir->finished)
        return NULL;

    if (dir->first_read)
    {
        dir->first_read = false;
    }
    else
    {
        if (!FindNextFileA(dir->hFind, &dir->findData))
        {
            dir->finished = true;
            return NULL;
        }
    }

    memset(&dir->ent, 0, sizeof(struct linux_dirent));
    dir->ent.d_ino = 1;
    dir->ent.d_off = 0;

    strncpy(dir->ent.d_name, dir->findData.cFileName, 260);
    dir->ent.d_reclen = (unsigned short)strlen(dir->ent.d_name);

    if (dir->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        dir->ent.d_type = 4;
    }
    else
    {
        dir->ent.d_type = 8;
    }

    return &dir->ent;
}

int hook_closedir(void *dirp)
{
    if (!dirp)
        return -1;
    DIR_Impl *dir = (DIR_Impl *)dirp;
    if (dir->hFind != INVALID_HANDLE_VALUE)
    {
        FindClose(dir->hFind);
    }
    free(dir);
    return 0;
}


int hook_unlink(const char *pathname)
{
    if (strncmp(pathname, "/tmp", 4) == 0)
    {
        pathname += 1;
    }
    return _unlink(pathname);
}

int hook_remove(const char *pathname)
{
    if (strncmp(pathname, "/home/disk1/rankingdata/", 25) == 0)
    {
        pathname += 11;
        char winPath[MAX_PATH];
        getKittenPath(pathname, winPath);
        return remove(winPath);
    }
    return remove(pathname);
}

static void CopyStatVer3(const struct _stat64 *src, void *dst_ptr)
{
    struct linux_stat_ver3 *dst = (struct linux_stat_ver3 *)dst_ptr;
    memset(dst, 0, sizeof(struct linux_stat_ver3));

    dst->st_dev = (uint64_t)src->st_dev;
    dst->st_ino = (uint32_t)src->st_ino;
    dst->st_nlink = src->st_nlink;
    dst->st_uid = src->st_uid;
    dst->st_gid = src->st_gid;
    dst->st_rdev = (uint64_t)src->st_rdev;
    dst->st_size = (int32_t)src->st_size; // 32-bit size
    dst->st_blksize = 4096;
    dst->st_blocks = (int32_t)((src->st_size + 511) / 512);
    dst->st_atime = (uint32_t)src->st_atime;
    dst->st_mtime = (uint32_t)src->st_mtime;
    dst->st_ctime = (uint32_t)src->st_ctime;

    dst->st_mode = 0;
    if (src->st_mode & _S_IFDIR)
        dst->st_mode |= LINUX_S_IFDIR;
    if (src->st_mode & _S_IFREG)
        dst->st_mode |= LINUX_S_IFREG;
    if (src->st_mode & _S_IFCHR)
        dst->st_mode |= LINUX_S_IFCHR;
    if (src->st_mode & _S_IFIFO)
        dst->st_mode |= LINUX_S_IFIFO;
    dst->st_mode |= (src->st_mode & 0777);
}

static void CopyStat64(const struct _stat64 *src, void *dst_ptr)
{
    struct linux_stat64_safe dst;
    memset(&dst, 0, sizeof(dst));

    dst.st_dev = (unsigned long long)src->st_dev;
    dst.__st_ino = (uint32_t)src->st_ino;
    dst.st_ino = (unsigned long long)src->st_ino;
    dst.st_nlink = src->st_nlink;
    dst.st_uid = src->st_uid;
    dst.st_gid = src->st_gid;
    dst.st_rdev = (unsigned long long)src->st_rdev;
    dst.st_size = src->st_size;
    dst.st_blksize = 4096;
    dst.st_blocks = (src->st_size + 511) / 512;
    dst.st_atime = (unsigned long)src->st_atime;
    dst.st_mtime = (unsigned long)src->st_mtime;
    dst.st_ctime = (unsigned long)src->st_ctime;

    dst.st_mode = 0;
    if (src->st_mode & _S_IFDIR)
        dst.st_mode |= LINUX_S_IFDIR;
    if (src->st_mode & _S_IFREG)
        dst.st_mode |= LINUX_S_IFREG;
    if (src->st_mode & _S_IFCHR)
        dst.st_mode |= LINUX_S_IFCHR;
    if (src->st_mode & _S_IFIFO)
        dst.st_mode |= LINUX_S_IFIFO;
    dst.st_mode |= (src->st_mode & 0777);

    memcpy(dst_ptr, &dst, sizeof(dst));
}

static int myStatImpl(const char *path, void *buf, bool use_stat64)
{
    if (strncmp(path, "/tmp", 4) == 0)
        path += 1;

    if (strncmp(path, "/var/tmp", 8) == 0)
        path += 5;

    if (path && (strstr(path, "/dev/") != NULL || strstr(path, "i2c/") != NULL || strstr(path, "ttyS") != NULL))
    {
        log_debug(plfBridges, "stat: Spoofing virtual device for %s", path);

        if (use_stat64)
        {
            struct linux_stat64_safe *s = (struct linux_stat64_safe *)buf;
            memset(s, 0, sizeof(*s));
            s->st_mode = LINUX_S_IFCHR | 0666;
            s->st_rdev = 1;
            s->st_nlink = 1;
        }
        else
        {
            struct linux_stat_ver3 *s = (struct linux_stat_ver3 *)buf;
            memset(s, 0, sizeof(*s));
            s->st_mode = LINUX_S_IFCHR | 0666;
            s->st_rdev = 1;
            s->st_nlink = 1;
        }
        return 0;
    }

    struct _stat64 win_stat;
    if (_stat64(path, &win_stat) != 0)
    {
        log_debug(plfBridges, "stat failed: %s", path);
        errno = ENOENT;
        return -1;
    }

    if (use_stat64)
    {
        CopyStat64(&win_stat, buf);
    }
    else
    {
        CopyStatVer3(&win_stat, buf);
    }

    return 0;
}

int hook_xstat(int ver, const char *path, struct linux_stat *buf)
{
    return myStatImpl(path, buf, false);
}
int hook_fxstat(int ver, int fd, struct linux_stat *buf)
{
    log_debug(plfBridges, "__fxstat called: ver=%d, fd=%d", ver, fd);
    if (!buf)
        return -1;
    struct _stat64 win_stat;
    if (_fstat64(fd, &win_stat) != 0)
        return -1;

    CopyStatVer3(&win_stat, buf);
    return 0;
}
int hook_fxstat64(int ver, int fd, struct linux_stat64 *buf)
{
    log_debug(plfBridges, "__fxstat64 called: ver=%d, fd=%d", ver, fd);
    if (!buf)
        return -1;
    struct _stat64 win_stat;
    if (_fstat64(fd, &win_stat) != 0)
        return -1;

    if (ver == 3)
    {
        CopyStatVer3(&win_stat, buf);
    }
    else
    {
        CopyStat64(&win_stat, buf);
    }
    return 0;
}

int hook_xmknod(int ver, const char *path, unsigned int mode, void *dev)
{
    return 0;
}

int hook_fcntl(int fd, int cmd, ...)
{
    return 0;
}

int hook_fputc(int c, FILE *stream)
{
    if (stream == stderr || stream == stdout)
    {
        if (c == 10) // ignore LF
            return 10;
    }

    int ret = fputc(c, stream);
    return ret;
}
