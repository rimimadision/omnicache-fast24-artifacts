#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <linux/pci.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "crfslibio.h"

static int delcounter;

/* Function pointers to hold the value of the glibc functions */
static ssize_t (*real_write)(int fd, const void *buf, size_t count) = NULL;
static ssize_t (*real_read)(int fd, void *buf, size_t count) = NULL;
static ssize_t (*real_pwrite)(int fd, const void *buf, size_t count, off_t offset) = NULL;
static ssize_t (*real_pread)(int fd, void *buf, size_t count, off_t offset) = NULL;

static int (*real_open)(const char *pathname, int flags, mode_t mode) = NULL;
static int (*real_close)(int fd) = NULL;
static int (*real_lseek64)(int fd, off_t offset, int whence) = NULL;

static int (*real_unlink)(const char *pathname) = NULL;
static int (*real_rename)(const char *oldpath, const char *newpath) = NULL;
static int (*real_fsync)(int fd) = NULL;

/* Initialize DevFS */
int crfsinit(unsigned int qentry_count,
		unsigned int dev_core_cnt, unsigned int sched_policy) {
#if !defined(_POSIXIO)
	initialize_crfs(qentry_count, dev_core_cnt, sched_policy, 0);
#endif
	return 0;
}

int crfsinit_cxl_ns(unsigned int qentry_count,
		unsigned int dev_core_cnt, unsigned int sched_policy, unsigned int cxl_ns_size) {
#if !defined(_POSIXIO)
	initialize_crfs(qentry_count, dev_core_cnt, sched_policy, cxl_ns_size);
#endif
	return 0;
}

/* Shutdown DevFS */
int crfsexit(void) {
#if !defined(_POSIXIO)
	shutdown_crfs();
#endif
	return 0;
}

/* wrapping write function call */
int crfslseek64(int fd, off_t offset, int whence) {
	int ret = 0;

#if !defined(_POSIXIO)
	ret = crfs_lseek64(fd, offset, whence);
	return ret;
#else
	real_lseek64 = dlsym(RTLD_NEXT, "lseek64");
	ret = real_lseek64(fd, offset, whence);
	return ret;
#endif
}


/* wrapping write function call */
ssize_t crfswrite(int fd, const void *buf, size_t count) {
	size_t sz;

#if !defined(_POSIXIO)
    /* printf("crfswrite\n"); */
	sz = crfs_write(fd, buf, count);
    /* printf("crfswrite done\n"); */
	return sz;
#else
	real_write = dlsym(RTLD_NEXT, "write");
	sz = real_write(fd, buf, count);
	return sz;	
#endif
}

ssize_t crfspwrite(int fd, const void *buf, size_t count, off_t offset) {
	size_t sz;

#if !defined(_POSIXIO)
    /* printf("crfspwrite\n"); */
	sz = crfs_pwrite(fd, buf, count, offset);
    /* printf("crfspwrite done\n"); */
	return sz;
#else
	real_pwrite = dlsym(RTLD_NEXT, "pwrite");
	sz = real_pwrite(fd, buf, count, offset);
	return sz;	
#endif
}


/* wrapping read function call */
ssize_t crfsread(int fd, void *buf, size_t count) {
	size_t sz;
	
#if !defined(_POSIXIO)
    /* printf("crfs read, fd: %d, count: %ld\n", count, fd); */
	sz = crfs_read(fd, buf, count);
    /* printf("crfs read, count: %ld, ret: %ld\n", count, sz); */
	return sz;
#else
	real_read = dlsym(RTLD_NEXT, "read");
	sz = real_read(fd, buf, count);
	return sz;
#endif
}


ssize_t crfspread(int fd, void *buf, size_t count, off_t offset) {
	size_t sz;
#if !defined(_POSIXIO)
    /* printf("pread, count: %ld, offset: %ld\n", count, offset); */
	sz = crfs_pread(fd, buf, count, offset);
    /* printf("pread, count: %ld, offset: %ld, ret: %ld\n", count, offset, sz); */
	return sz;
#else
	real_pread = dlsym(RTLD_NEXT, "pread");
	sz = real_pread(fd, buf, count, offset);
	return sz;
#endif
}


/* wrapping open function call */
int crfsopen(const char *pathname, int flags, mode_t mode)
{
	int fd = -1;

#if !defined(_POSIXIO)
    /* printf("crfsopen, filename: %s\n", pathname); */
	fd = crfs_open_file(pathname, flags, mode | O_RDWR);
    /* printf("crfsopen, filename: %s, fd: %d\n", pathname, fd); */
#else
	real_open = dlsym(RTLD_NEXT, "open");
	real_open(pathname, flags, mode);
#endif
	return fd;
}


/* wrapping unlink function call */
int crfsunlink(const char *pathname)
{
	int ret = 0;

#if !defined(_POSIXIO)
    /* printf("unlink\n"); */
	ret = crfs_unlink(pathname);
    /* printf("unlink done\n"); */
#else
	real_unlink = dlsym(RTLD_NEXT, "unlink");
	real_unlink(pathname);
#endif
	return ret;
}

/* wrapping fsync function call */
int crfsfsync(int fd)
{
	int ret = 0;
#if !defined(_POSIXIO)
	ret = crfs_fsync(fd);
#else
	real_fsync = dlsym(RTLD_NEXT, "fsync");
	ret = real_fsync(fd);
#endif
	return ret;
}


/* wrapping open function call */
int crfsclose(int fd)
{

//#if defined(_DEBUG)
    /* printf("Close#:%d\n", fd); */
//#endif

#if !defined(_POSIXIO)
    /* printf("Close#:%d\n", fd); */
    int ret = crfs_close_file(fd); 
    /* printf("Close#:%d done\n", fd); */
	return ret;
#else
	real_close = dlsym(RTLD_NEXT, "close");
	real_close(fd);
#endif
}


int crfsfallocate(int fd, int mode, off_t offset, off_t len)
{
        int ret = 0;
#if !defined(_POSIXIO)
        ret = crfs_fallocate(fd, offset, len);
#else
        real_fallocate = dlsym(RTLD_NEXT, "fallocate");
        ret = real_fallocate(fd, mode, offset, len);
#endif
        return ret;

}


int crfsftruncate(int fd, off_t length)
{
        int ret = 0;
#if !defined(_POSIXIO)
        ret = crfs_ftruncate(fd, length);
#else
        real_ftruncate = dlsym(RTLD_NEXT, "ftruncate");
        ret = real_ftruncate(fd, length);
#endif
        return ret;
}


int crfsrename(const char *oldpath, const char *newpath)
{
        int ret = 0;
#if !defined(_POSIXIO)
        ret = crfs_rename(oldpath, newpath);
#else
        real_rename = dlsym(RTLD_NEXT, "rename");
        ret = real_rename(oldpath, newpath);
#endif
        return ret;

}

/* Compound POSIX calls (containing computing along with I/O command) */
int devfsreadmodifywrite(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_readmodifywrite(fd, buf, count, offset);
        return sz;
}

int devfsreadmodifyappend(int fd, const void *buf, size_t count) {
        size_t sz = devfs_readmodifyappend(fd, buf, count);
        return sz;
}


size_t devfsopenpwriteclose_batch(int fd, const void **p, size_t count, off_t* offsets, char* out_file, size_t batch_size){

        size_t sz = devfs_open_pwrite_close_batch(fd, p, count, offsets, out_file, batch_size);
        return sz;
}
size_t devfschecksumpwrite_batch(int fd, const void **p, size_t count, 
                off_t* offsets, uint8_t crc_pos, size_t batch_size) {
        size_t sz = devfs_checksumpwrite_batch(fd, p, count, offsets, crc_pos, batch_size);
        return sz;
}

size_t devfsreadmodifywrite_batch(int fd, const void **p, size_t count, off_t* offsets, size_t batch_size){
        size_t sz = devfs_readmodifywrite_batch(fd, p, count, offsets, batch_size);
        return sz;
}

int devfschecksumread(int fd, void *buf, size_t count) {
        size_t sz = devfs_checksumread(fd, buf, count, 1);
        return sz;
}

int devfschecksumwrite(int fd, const void *buf, size_t count) {
        size_t sz = devfs_checksumwrite(fd, buf, count, 1);
        return sz;
}

int devfschecksumpread(int fd, void *buf, size_t count, off_t offset) {
        size_t sz = devfs_checksumpread(fd, buf, count, offset, 1);
        return sz;
}

int devfschecksumpwrite(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_checksumpwrite(fd, buf, count, offset, 1);
        return sz;
}

int devfschecksumpwrite_cache_host(int fd, const void *buf, size_t count, off_t offset) {
    size_t sz = devfs_checksumpwrite_cache_host(fd, buf, count, offset, 1);
    return sz;
}

size_t devfsappendchksmpwrite_cache_hybrid(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_appendchksmpwrite_cache_hybrid(fd, buf, count, offset, 1);
        return sz;
}

int devfsappendchksmpwrite_cache_kernel(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_appendchksmpwrite_cache_kernel(fd, buf, count, offset, 1);
        return sz;
}

int devfsreadchksmpwrite(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_readchksmpwrite(fd, buf, count, offset, 1);
        return sz;
}

int devfsreadchksmpwrite_cache_host(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_readchksmpwrite_cache_host(fd, buf, count, offset, 1);
        return sz;
}

int devfsreadchksmpwrite_cache_kernel(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_readchksmpwrite_cache_kernel(fd, buf, count, offset, 1);
        return sz;
}

int devfsreadchksmpwrite_cache_hybrid(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_readchksmpwrite_cache_hybrid(fd, buf, count, offset, 1);
        return sz;
}

int devfscompresswrite_cache(int fd, const void *buf, size_t count, char* in_file) {
        size_t sz = devfs_compresswrite_cache(fd, buf, count, in_file);
        return sz;
}

int devfscompresswrite(int fd, const void *buf, size_t count, char* in_file) {
        size_t sz = devfs_compresswrite(fd, buf, count, in_file);
        return sz;
}

/* Leveldb related compound POSIX calls */
size_t devfs_leveldb_checksum_write(int fd, const void* data, size_t data_len, char* meta,
                size_t meta_len, int checksum_pos, int type_pos, uint8_t end, int cal_type, uint32_t type_crc) {
        size_t sz = leveldb_checksumwrite(fd, data, data_len, meta, meta_len,
                        checksum_pos, type_pos, end, cal_type, type_crc);
        return sz;
}

ssize_t devfs_leveldb_checksum_read(int fd, void *buf, size_t count, uint8_t end) {
        ssize_t sz = leveldb_checksumread(fd, buf, count, end);
        return sz;
}

ssize_t devfs_leveldb_checksum_pread(int fd, void *buf, size_t count, off_t offset, uint8_t end) {
        ssize_t sz = leveldb_checksumpread(fd, buf, count, offset, end);
        return sz;
}


/* Empty LevelDB calls. Used for shim library. */
size_t leveldb_checksum_write(int fd, const void* data, size_t data_len, char* meta,
                size_t meta_len, int checksum_pos, int type_pos, uint8_t end, int cal_type, uint32_t type_crc) {
        size_t sz = leveldb_checksumwrite(fd, data, data_len, meta, meta_len,
                        checksum_pos, type_pos, end, cal_type, type_crc);
        return sz;
}

ssize_t leveldb_checksum_read(int fd, void *buf, size_t count, uint8_t end) {
        ssize_t sz = leveldb_checksumread(fd, buf, count, end);
        return sz;
}

ssize_t leveldb_checksum_pread(int fd, void *buf, size_t count, off_t offset, uint8_t end) {
        ssize_t sz = leveldb_checksumpread(fd, buf, count, offset, end);
        return sz;
}

int devfsencryptwrite(int fd, const void *buf, size_t count) {
        size_t sz = devfs_encryptwrite(fd, buf, count);
        return sz;
}

int devfsencryptpwrite(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_encryptpwrite(fd, buf, count, offset);
        return sz;
}

int devfspreadencryptpwrite(int fd, const void *buf, size_t count, off_t offset) {
        size_t sz = devfs_preadencryptpwrite(fd, buf, count, offset);
        return sz;
}

ssize_t devfsopenpwriteclose(int fd, const void *buf, size_t count, off_t offset, char* out_file) {
        size_t sz;
        sz = devfs_open_pwrite_close(fd, buf, count, offset, out_file);
        return sz;
}

ssize_t devfsreadappend(int fd, const void *buf, size_t count) {
        size_t sz;
        sz = devfs_read_append(fd, buf, count);
        return sz;
}

size_t devfsopenpreadclose(int fd, const void *buf, size_t count, off_t offset, char* filename) {
        size_t sz;
        sz = devfs_open_pread_close(fd, buf, count, offset, filename);
        return sz;
}

int crfschecksumwritecc(int fd, const void *buf, size_t count) {
        size_t sz = crfs_checksumwritecc(fd, buf, count, 1);
        return sz;
}


size_t devfs_readknnwrite(int fd, const void *p, size_t count, off_t offset) {
        size_t sz = devfs_read_knn_write(fd, p, count, offset);
        return sz;
}

/* Inject Crash calls */
int crfsinjectcrash(int fd, int crash_code) {
        int ret = 0;
        ret = crfs_injectcrash(fd, crash_code);
        return ret;
}
