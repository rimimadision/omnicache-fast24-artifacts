#define _GNU_SOURCE
#include "shim_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#include <errno.h>
#include <unistd.h>
#include <linux/limits.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>

#include "unvme_nvme.h"
#include "crfslib.h"

#define RECORD_PATH "io_syscall_record.txt"
#define RECORD_LENGTH 4096
#define FILEPERM 0666
#define OPEN_FILE_MAX 1048576
#define PATH_BUF_SIZE 4095
#define DEVFS_PREFIX (char *)"/mnt/ram"
#define syscall_trace(...)


#ifdef __cplusplus
// extern "C" {
#endif
/* static int devfs_fd_table[OPEN_FILE_MAX] = {0}; */
static int initialized = 0;
static FILE *rec_file = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct devfs_file {
    int devfs_fd;
    off_t offset;
};

static struct devfs_file* fd_file_table[OPEN_FILE_MAX] = {NULL};

int str_to_oflags(const char* mode) {
    int ret;
    int len = strlen(mode);
    char* tmp = (char*)malloc(len*sizeof(char)+1);
    int index = 0;
    for(int i=0; i<strlen(mode); i++) {
        if (mode[i] != 'b') tmp[index++] = mode[i];

    }
    tmp[index] = '\0';
    if (strcmp(tmp, "r") == 0) ret = O_RDONLY;
    else if (strcmp(tmp, "r+") == 0) ret = O_RDWR;
    else if (strcmp(tmp, "w") == 0) ret = O_WRONLY | O_TRUNC | O_CREAT;
    else if (strcmp(tmp, "w+") == 0) ret = O_RDWR | O_TRUNC | O_CREAT;
    else if (strcmp(tmp, "a") == 0) ret = O_WRONLY | O_CREAT | O_APPEND;
    else if (strcmp(tmp, "a+") == 0) ret = O_RDWR | O_CREAT | O_APPEND;
    else ret = 0;
    free(tmp);
    return ret;

}

FILE* fopen(const char *filename, const char* perm) {
    if (real_fopen == NULL) real_fopen = (FILE* (*)(const char*, const char*))dlsym(RTLD_NEXT, "fopen");
    
    FILE *ret = NULL;
    FILE *fake_file = NULL;
    int fake_fd = 0;
    int devfs_fd = 0;
    char fullpath[PATH_BUF_SIZE];

    memset(fullpath, 0, PATH_BUF_SIZE);

    if (filename[0] == '/') {                                                                                                                                                
        strcpy(fullpath, filename);
    } else {
        getcwd(fullpath, sizeof(fullpath));
        strcat(fullpath, "/");
        strcat(fullpath, filename);
    }   

    if (strncmp(fullpath, "/mnt/ram", 8) ||
            !strcmp(fullpath, "/mnt/ram/test")){
        return real_fopen(filename, perm);
    } else {
        int oflags = str_to_oflags(perm);
        struct devfs_file* df = NULL;
        //printf("fopen: full path: %s, perm: %s, oflags: %d\n", fullpath, perm, oflags);
        devfs_fd = crfsopen(fullpath, oflags, FILEPERM);
        if (devfs_fd > 0)  {
            df = (struct devfs_file*)malloc(sizeof(struct devfs_file));
            df->devfs_fd = devfs_fd;
            df->offset = 0;
            fake_file = tmpfile();
            if (fake_file == NULL) return NULL;
            fake_fd = fileno(fake_file);
            fd_file_table[fake_fd] = df;
            return fake_file;
        }
        return ret;
    }
}

int open(const char *filename, int flags, ...) {
    real_open = (int (*)(const char*, int, ...))dlsym(RTLD_NEXT, "open");

    int ret;
    char fullpath[PATH_BUF_SIZE];
    mode_t mode = 0;
    if ((flags & O_CREAT) == O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    if (flags > 0x4000)
        return real_open(filename, flags, mode);


    memset(fullpath, 0, PATH_BUF_SIZE);

    if (filename[0] == '/') {                                                                                                                                                
        strcpy(fullpath, filename);
    } else {
        getcwd(fullpath, sizeof(fullpath));
        strcat(fullpath, "/");
        strcat(fullpath, filename);
    }   

    if (strncmp(fullpath, "/mnt/ram", 8) ||
            !strcmp(fullpath, "/mnt/ram/test")){
        return real_open(filename, flags, mode);
    } else {
        struct devfs_file* df = NULL;
        ret = crfsopen(fullpath, flags, mode);
        if (ret > 0)  {
            struct devfs_file* df = (struct devfs_file*)malloc(sizeof(struct devfs_file));
            df->devfs_fd = ret;
            df->offset = 0;
            FILE* fake_file = tmpfile();
            if (fake_file == NULL) return 0;
            int fake_fd = fileno(fake_file);
            fd_file_table[fake_fd] = df;
            //printf("open: full path: %s, oflags: %d, fd: %d, fake_fd: %d\n", filename, flags, df->devfs_fd, fake_fd);
            return fake_fd;
        }
        return ret;
    }
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (real_fread == NULL) real_fread = (size_t (*)(void*, size_t, size_t, FILE*))dlsym(RTLD_NEXT, "fread");
    size_t ret;
    struct devfs_file* df = NULL;
    int fake_fd = fileno(stream);
    df = fd_file_table[fake_fd];
    if (df != NULL) {
        ret = crfspread(df->devfs_fd, ptr, nmemb * size, df->offset);
        if (ret > 0) {
            df->offset += ret;
        }
        //printf("fread, fd: %d, buf: %s, ret: %d\n", df->devfs_fd, (char *)ptr, ret);
        return ret;
    } else {
        return real_fread(ptr, size, nmemb, stream);
    }
}

size_t fread_unlocked(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fread(ptr, size, nmemb, stream);
}

ssize_t read(int fd, void *buf, size_t count) {
    real_read = (ssize_t (*)(int, void*, size_t))dlsym(RTLD_NEXT, "read");
    size_t ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;  
        ret = crfsread(fd, buf, count);
        return ret;
    } else {
        return real_read(fd, buf, count);
    }
}

ssize_t pread(int fd, void *buf, size_t count, off_t off) {
    real_pread = (ssize_t (*)(int, void*, size_t, off_t))dlsym(RTLD_NEXT, "pread");

    size_t ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;  
        ret = crfspread(fd, buf, count, off);
        return ret;
    } else {
        return real_pread(fd, buf, count, off);;
    }
}


ssize_t pread64(int fd, void *buf, size_t count, off_t off) {
    real_pread64 = (ssize_t (*)(int, void*, size_t, off_t))dlsym(RTLD_NEXT, "pread64");

    size_t ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfspread(fd, buf, count, off);
        return ret;
    } else {
        return real_pread64(fd, buf, count, off);;
    }
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (real_fwrite == NULL) real_fwrite = (size_t (*)(const void*, size_t, size_t, FILE*))dlsym(RTLD_NEXT, "fwrite");
    size_t ret;
    struct devfs_file* df = NULL;
    int fake_fd = fileno(stream);
    df = fd_file_table[fake_fd];
    if (df != NULL) {
        ret = crfspwrite(df->devfs_fd, ptr, nmemb * size, df->offset);
        if (ret > 0) {
            df->offset += ret;
        }
        //printf("fwrite, fd: %d, buf: %s\n",df->devfs_fd, (char *) ptr);
        return ret;
    } else {
        return real_fwrite(ptr, size, nmemb, stream);;
    }

}

size_t fwrite_unlocked(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

ssize_t write(int fd, const void *buf, size_t count) {
    real_write = (ssize_t (*)(int, const void*, size_t))dlsym(RTLD_NEXT, "write");
    size_t ret;

    //printf("write\n");
    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfswrite(fd, buf, count);
        return ret;
    } else {
        return real_write(fd, buf, count);
    }
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t off) {
    real_pwrite = (ssize_t (*)(int, const void*, size_t, off_t))dlsym(RTLD_NEXT, "pwrite");
    size_t ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfspwrite(fd, buf, count, off);
        return ret;
    } else {
        return real_pwrite(fd, buf, count, off);;
    }
}

ssize_t pwrite64(int fd, const void *buf, size_t count, off_t off) {
    real_pwrite64 = (ssize_t (*)(int, const void*, size_t, off_t))dlsym(RTLD_NEXT, "pwrite64");
    size_t ret;


    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfspwrite(fd, buf, count, off);
        return ret;
    } else {
        return real_pwrite64(fd, buf, count, off);;
    }
}


int fclose(FILE* file) {
    if (real_fclose == NULL) real_fclose = (int (*)(FILE*))dlsym(RTLD_NEXT, "fclose");
    int ret;
    int fake_fd = fileno(file);
    int fd = 0;
    struct devfs_file* df = NULL;
    df = fd_file_table[fake_fd];
    if (df != NULL) {
        fd = df->devfs_fd;
        ret = crfsclose(fd);
        fd_file_table[fake_fd] = NULL;
        free(df);
        //printf("fclose, fd: %d, fake_fd: %d\n", fd, fake_fd);
        return ret;
    } else {
        return real_fclose(file);
    }

}

int close(int fd) {
    if (real_close == NULL) real_close = (int (*)(int))dlsym(RTLD_NEXT, "close");
    int ret;
    int fake_fd = fd;
    struct devfs_file* df = NULL;
    df = fd_file_table[fake_fd];
    if (df != NULL) {
        fd = df->devfs_fd;
        ret = crfsclose(fd);
        fd_file_table[fake_fd] = NULL; 
        free(df);
        //printf("close, fd: %d, fake_fd: %d\n", fd, fake_fd);
        return ret;
    } else {
        return real_close(fd);
    }
}

int fseek(FILE *stream, long offset, int whence) {

    if (real_fseek == NULL) real_fseek = (int (*)(FILE*, long, int))dlsym(RTLD_NEXT, "fseek");
    int ret;
    int fd = fileno(stream);
    struct devfs_file* df = NULL;
    df = fd_file_table[fd];
    if (df != NULL) {
	if (whence == SEEK_SET) {
            df->offset = offset;
        } else if (whence == SEEK_CUR) {
            df->offset += offset;
        } else {
            // TODO
        }
        ret = df->offset;
        return ret;
    } else {
        return real_fseek(stream, offset, whence);
    }
}

long ftell(FILE *stream) {
    if (real_ftell == NULL) real_ftell = (long (*)(FILE*))dlsym(RTLD_NEXT, "ftell");
    int fd = fileno(stream);
    struct devfs_file* df = NULL;
    df = fd_file_table[fd];
    if (df != NULL) {
        return df->offset;
    } else {
        return real_ftell(stream);
    }

}


off_t lseek(int fd, off_t offset, int origin) {
    real_lseek = (off_t (*)(int, off_t, int))dlsym(RTLD_NEXT, "lseek");
    int ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfslseek64(fd, offset, origin);
        return ret;
    } else {
        return real_lseek(fd, offset, origin);
    }
}

off_t lseek64(int fd, off_t offset, int origin) {
    real_lseek64 = (off_t (*)(int, off_t, int))dlsym(RTLD_NEXT, "lseek64");
    int ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfslseek64(fd, offset, origin);
        return ret;
    } else {
        return real_lseek64(fd, offset, origin);
    }
}

int rename(const char *oldname, const char *newname) {
    if (real_rename == NULL) real_rename = (int (*)(const char*, const char*))dlsym(RTLD_NEXT, "rename");
    int ret;
    char fullpathold[PATH_BUF_SIZE];
    char fullpathnew[PATH_BUF_SIZE];

    memset(fullpathold, 0, PATH_BUF_SIZE);
    memset(fullpathnew, 0, PATH_BUF_SIZE);

    if (oldname[0] == '/') {
        strcpy(fullpathold, oldname);
    } else {
        getcwd(fullpathold, sizeof(fullpathold));
        strcat(fullpathold, "/");
        strcat(fullpathold, oldname);
    }

    if (newname[0] == '/') {
        strcpy(fullpathnew, newname);
    } else {
        getcwd(fullpathnew, sizeof(fullpathnew));
        strcat(fullpathnew, "/");
        strcat(fullpathnew, newname);
    }

    if (strncmp(fullpathold, "/mnt/ram", 8) ||
            !strcmp(fullpathold, "/mnt/ram/test")){
        return real_rename(oldname, newname);
    } else {
        ret = crfsrename(fullpathold, fullpathnew);
        syscall_trace(__func__, ret, 2, oldname, newname);
        return ret;
    }

    return real_rename(oldname, newname);

}

int fallocate(int fd, int mode, off_t offset, off_t len) {
    real_fallocate = (int (*)(int, int, off_t, off_t))dlsym(RTLD_NEXT, "fallocate");
    int ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfsfallocate(fd, mode, offset, len);

        return ret;
    } else {
        return real_fallocate(fd, mode, offset, len);
    }
}

int ftruncate(int fd, off_t length) {
    if (real_ftruncate == NULL ) real_ftruncate = (int (*)(int, off_t))dlsym(RTLD_NEXT, "ftruncate");
    int ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfsftruncate(fd, length);

        return ret;
    } else {
        return real_ftruncate(fd, length);
    }
}

int unlink(const char *path) {
    if (real_unlink == NULL) real_unlink = (int (*)(const char*))dlsym(RTLD_NEXT, "unlink");

    int ret;
    char fullpath[PATH_BUF_SIZE];

    memset(fullpath, 0, PATH_BUF_SIZE);

    if (path[0] == '/') {
        strcpy(fullpath, path);
    } else {
        getcwd(fullpath, sizeof(fullpath));
        strcat(fullpath, "/");
        strcat(fullpath, path);
    }

    if (strncmp(fullpath, "/mnt/ram", 8) ||
            !strcmp(fullpath, "/mnt/ram/test")){
        return real_unlink(path);
    } else {
        ret = crfsunlink(fullpath);
        syscall_trace(__func__, ret, 1, path);
        return ret;
    }
}

int fflush(FILE* stream) {
    if (real_fflush == NULL) real_fflush = (int (*)(FILE*))dlsym(RTLD_NEXT, "fflush");
    int fd = fileno(stream);
    int ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfsfsync(fd);    
        return ret;
    } else {
        return real_fflush(stream);
    }
}

int fflush_unlocked(FILE* stream) {
    return fflush(stream);
}

int fsync(int fd) {
    real_fsync = (int (*)(int))dlsym(RTLD_NEXT, "fsync");
    int ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfsfsync(fd);    
        return ret;
    } else {
        return real_fsync(fd);
    }
}


int fdatasync(int fd) {
    real_fdatasync = (int (*)(int))dlsym(RTLD_NEXT, "fdatasync");
    int ret;

    if (fd_file_table[fd] != NULL) {
        fd = fd_file_table[fd]->devfs_fd;
        ret = crfsfsync(fd);    
        return ret;
    } else {
        return real_fdatasync(fd);
    }
}

char* fgets(char* s, int size, FILE* stream) {
    if (real_fgets == NULL) real_fgets = (char* (*)(char*, int, FILE*))dlsym(RTLD_NEXT, "fgets");

    char* result = NULL;
    int ret = 0;
    int fd = fileno(stream);
    if (fd_file_table[fd]  == NULL) {
        result = real_fgets(s, size, stream);
    } else {
        struct devfs_file* df = fd_file_table[fd];
        ssize_t bytes;
        ret = crfspread(df->devfs_fd, s, size, df->offset);

        if (ret < 0) {
            stream->_flags |= _IO_ERR_SEEN;
            result = NULL;
        }
        else if (bytes == 0) {
            stream->_flags |= _IO_EOF_SEEN;
            result = NULL;
        }
        else {
            char* eol = (char*)memchr((void*)s, '\n', ret);
            if (eol == NULL) {
                s[size-1] = '\0';
                fseek(stream, size, SEEK_CUR);
            }
            else {
                *(++eol) = '\0';
                //printf("fgets, size: %d, read size: %d\n", size, eol-s);
                fseek(stream, eol-s, SEEK_CUR);
            }
            result = s;
        }
    }
    return result;
}

size_t leveldb_checksum_write(int fd, const void* data, size_t data_len, char* meta,
                size_t meta_len, int checksum_pos, int type_pos, uint8_t end, int cal_type, uint32_t type_crc) {
        
        size_t sz = 0;
        if (fd_file_table[fd] != NULL) {
                fd = fd_file_table[fd]->devfs_fd;
                sz = devfs_leveldb_checksum_write(fd, data, data_len, meta, meta_len,
                                checksum_pos, type_pos, end, cal_type, type_crc);
        } else {
                printf("shim error\n");
        }
        return sz;
}

ssize_t leveldb_checksum_read(int fd, void *buf, size_t count, uint8_t end) {
        size_t sz = 0;
        if (fd_file_table[fd] != NULL) {
                fd = fd_file_table[fd]->devfs_fd;
                sz = devfs_leveldb_checksum_read(fd, buf, count, end);
        } else {
                printf("shim error\n");
        }
        return sz;
}

ssize_t leveldb_checksum_pread(int fd, void *buf, size_t count, off_t offset, uint8_t end) {
        size_t sz = 0;
        if (fd_file_table[fd] != NULL) {
                fd = fd_file_table[fd]->devfs_fd;
                sz = devfs_leveldb_checksum_pread(fd, buf, count, offset, end);
        } else {
                printf("shim error\n");
        }
        return sz;
}

#ifdef __cplusplus
// }
#endif
