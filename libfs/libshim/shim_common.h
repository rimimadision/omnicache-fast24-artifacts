
#ifndef __SHIM_COMMON_H_
#define __SHIM_COMMON_H_
#include <dirent.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

static int (*real_open)(const char*, int, ...) = NULL;
static int (*real_open64)(const char*, int, ...) = NULL;

static int (*real_close)(int) = NULL;
static ssize_t (*real_read)(int, void*, size_t) = NULL;
static ssize_t (*real_write)(int, const void*, size_t) = NULL;
static int (*real_chmod)(const char*, mode_t) = NULL;
static int (*real_fchmod)(int, mode_t) = NULL;
static int (*real_access)(const char*, int) = NULL;
static int (*real_chown)(const char*, uid_t, gid_t) = NULL;
static int (*real_fchown)(int, uid_t, gid_t) = NULL;
static int (*real_creat)(const char*, mode_t) = NULL;
static int (*real_dup)(int) = NULL;
static int (*real_dup2)(int, int) = NULL;
static int (*real_dup3)(int, int, int) = NULL;
static int (*real_utimes)(const char*, const struct timeval[2]) = NULL;
static int (*real_futimes)(int, const struct timeval[2]) = NULL;
static int (*real_futimens)(int, const struct timespec[2]) = NULL;
static ssize_t (*real_pread)(int, void*, size_t, off_t) = NULL;
static ssize_t (*real_pwrite)(int, const void*, size_t, off_t) = NULL;
static ssize_t (*real_pread64)(int, void*, size_t, off_t) = NULL;
static ssize_t (*real_pwrite64)(int, const void*, size_t, off_t) = NULL;
static int (*real_truncate)(const char*, off_t) = NULL;
static void (*real_sync)() = NULL;
static int (*real_syncfs)(int) = NULL;
static int (*real_fsync)(int) = NULL;
static int (*real_fdatasync)(int) = NULL;
static FILE* (*real_fopen)(const char*, const char*) = NULL;
static int (*real_fclose)(FILE*) = NULL;
static size_t (*real_fread)(void*, size_t, size_t, FILE*) = NULL;
static size_t (*real_fwrite)(const void*, size_t, size_t, FILE*) = NULL;
static int (*real_fscanf)(FILE*, const char*, ...) = NULL;
static int (*real_fgetc)(FILE*) = NULL;
static char* (*real_fgets)(char*, int, FILE*) = NULL;
static int (*real_ungetc)(int, FILE*) = NULL;
static int (*real_fputc)(int, FILE*) = NULL;
static int (*real_fputs)(const char*, FILE*) = NULL;
static int (*real_vfprintf)(FILE*, const char*, va_list) = NULL;
static int (*real_vdprintf)(int, const char*, va_list) = NULL;
static int (*real_fflush)(FILE*) = NULL;
static ssize_t (*real_readlink)(const char*, char*, size_t) = NULL;
static int (*real_link)(const char*, const char*) = NULL;
static int (*real_symlink)(const char*, const char*) = NULL;
static int (*real_unlink)(const char*) = NULL;
static int (*real_rename)(const char*, const char*) = NULL;
static int (*real_fcntl)(int, int, ...) = NULL;
static int (*real_ftruncate)(int, off_t) = NULL;
static DIR* (*real_opendir)(const char*) = NULL;
static int (*real_closedir)(DIR*) = NULL;
static struct dirent* (*real_readdir)(DIR*) = NULL;
static void (*real_rewinddir)(DIR*) = NULL;
static void (*real_seekdir)(DIR*, long) = NULL;
static long (*real_telldir)(DIR*) = NULL;
static int (*real_mkdir)(const char*, mode_t) = NULL;
static int (*real_rmdir)(const char*) = NULL;
static off_t (*real_lseek)(int fd, off_t offset, int whence) = NULL;
static off_t (*real_lseek64)(int fd, off_t offset, int whence) = NULL;
static int (*real_fallocate)(int fd, int mode, off_t offset, off_t len) = NULL;
static int (*real_fseek)(FILE *stream, long offset, int whence) = NULL;
static long (*real_ftell)(FILE *stream) = NULL;
static int (*real_feof)(FILE *stream) = NULL;

#endif

