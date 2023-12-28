#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../crc32/crc32_defs.h"
#include "../crfslib.h"
#include "../unvme_nvme.h"

#ifdef _POSIX
#define TESTDIR "/mnt/pmemdir"
//#define TESTDIR "/dev/shm"
#define INPUT_DIR "/mnt/pmemdir/dataset"
#else
#define TESTDIR "/mnt/ram"
#define INPUT_DIR "/mnt/ram/dataset"
#endif

#define KB (1024UL)
#define MB (1024 * KB)
#define GB (1024 * MB)
#define FILEPERM 0666

#define CHECKSUMSIZE 4
#define MAGIC_SEED 0

#define READ_MODIFY_WRITE_SYS_NUM 548
#define WRITE_CHECKSUM_SYS_NUM 549

/*
 * By default, thread # is fixed to 4
 * and block size (a.k.a value size) is 4096
 */
int thread_nr = 4;
int filenum = 0;
int BLOCKSIZE = 4096;
uint64_t ITERS = 0;
unsigned long FILESIZE = 0;
int FILENUM = 6000;
int batch_size = 5;

pthread_mutex_t g_lock;
double g_avgthput = 0;

pthread_t *tid;
int *thread_idx;

/* benchmark function pointer */
void *(*benchmark)(void *) = NULL;

/* To calculate simulation time */
double simulation_time(struct timeval start, struct timeval end) {
        double current_time;
        current_time = ((end.tv_sec + end.tv_usec * 1.0 / 1000000) -
                        (start.tv_sec + start.tv_usec * 1.0 / 1000000));
        return current_time;
}

double simulation_time_us(struct timeval start, struct timeval end) {
        double current_time;
        current_time =  (end.tv_sec - start.tv_sec)*1.0 * 1000000 + (end.tv_usec - start.tv_usec);
        return current_time;
}

typedef unsigned long long int UINT64;

UINT64 getRandom(UINT64 min, UINT64 max)
{
    return (((UINT64)(unsigned int)rand() << 32) + (UINT64)(unsigned int)rand()) % (max - min) + min;

}

#ifndef COMPOUND
void *do_append_checksum_write(void *arg) {
        char *buf = malloc(BLOCKSIZE);
        uint64_t i = 0, j = 0;
        int fd = 0;
        int thread_id = *(int *)arg;
        int range = ITERS / thread_nr;
        double start = thread_id * range;
        double end = (thread_id + 1) * range;
        uint32_t crc = 0;
        double sec = 0.0;
        double thruput = 0;
        struct timeval start_t, end_t;

        if ((fd = crfsopen(TESTDIR "/testfile", O_RDWR, FILEPERM)) < 0) {
                perror("creat");
                return NULL;
        }

        gettimeofday(&start_t, NULL);

        for (i = start; i < end; i++) {
                /* Write new content to this block */
                for (int j = 0; j < BLOCKSIZE; j++) {
                        buf[j] = 0x41 + j % 26;
                }

                /* Calculate checksum */
                crc = crc32(MAGIC_SEED, buf, BLOCKSIZE - CHECKSUMSIZE);

                /* Put checksum into write block */
                memcpy(buf + BLOCKSIZE - CHECKSUMSIZE, (void *)&crc,
                       CHECKSUMSIZE);

#ifndef WRITE_SEPARATE
                /* Write back new data with checksum */
                if (crfspwrite(fd, buf, BLOCKSIZE, i * BLOCKSIZE) !=
                    BLOCKSIZE) {
                        printf("File data block checksum write fail \n");
                        return NULL;
                }

#else
                /* Write back new data */
                if (crfspwrite(fd, buf, BLOCKSIZE - CHECKSUMSIZE,
                               i * BLOCKSIZE) != BLOCKSIZE - CHECKSUMSIZE) {
                        printf("File data block write fail \n");
                        return NULL;
                }

                /* Write back checksum */
                if (crfspwrite(fd, buf + BLOCKSIZE - CHECKSUMSIZE, CHECKSUMSIZE,
                               i * BLOCKSIZE + BLOCKSIZE - CHECKSUMSIZE) !=
                    CHECKSUMSIZE) {
                        printf("File data block checksum fail \n");
                        return NULL;
                }

#endif
        }

        gettimeofday(&end_t, NULL);
        sec = simulation_time(start_t, end_t);
        thruput = (double)((end - start) * BLOCKSIZE) / sec;

        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);

        crfsclose(fd);
        free(buf);
}

void *do_read_checksum_write(void *arg) {
        char *buf = malloc(BLOCKSIZE);
        uint64_t i = 0;
        uint64_t k = 0;
        int thread_id = *(int *)arg;
        int range = ITERS / thread_nr;
        double start = thread_id * range;
        double end = (thread_id + 1) * range;
        double sec = 0.0;
        uint32_t crc = 0;
        double thruput = 0;
        char test_file[256];
        int file_num_per_thd = 1;
        int file_num_start = 0;
        int file_num_end = 1;
        int* fd = (int *)malloc(512 * sizeof(int));

        if (filenum >= thread_nr) {
                file_num_per_thd = (filenum / thread_nr);
                file_num_start = thread_id * file_num_per_thd;
                file_num_end = (thread_id + 1) * file_num_per_thd;
                start = 0;
                end = ITERS;
        }

        /* printf("filenum: %d, thread_nr: %d, thread_id: %d, iters: %ld\n", filenum, thread_nr, thread_id, ITERS); */
        
        struct timeval start_t, end_t;
        struct timeval start_tt, end_tt;

        for (i = file_num_start, k = 0; i < file_num_end; i++, k++) {
                snprintf(test_file, 256, "%s/testfile%ld", TESTDIR, i);
                if ((fd[k] = crfsopen(test_file, O_RDWR, FILEPERM)) < 0) {
                        perror("creat");
                        return NULL;
                }
                /* printf("thread_id: %d, file: %s, fd: %d\n", thread_id, test_file, fd[k]); */
        }

        gettimeofday(&start_t, NULL);
        for (k = 0; k < file_num_per_thd; k++) {
                for (i = start; i < end; i++) {
                        /* Write new content to this block */
                        for (int j = 0; j < BLOCKSIZE; j++) {
                                buf[j] = 0x41 + j % 26;
                        }
                        /* Issue a checksum pwrite directly, offload to storage
                         */
                        /* printf("devfschecksumpwrite, fd: %d, blocksize: %d, offset: %ld, k: %d, file_num_per_thd: %d\n", fd[k], BLOCKSIZE, i*BLOCKSIZE, k, file_num_per_thd); */
                        /* gettimeofday(&start_tt, NULL); */

                        if (pread(fd[k], buf, BLOCKSIZE, i*BLOCKSIZE) != BLOCKSIZE) {
                                printf(
                                    "File data block checksum write fail \n");
                                return NULL;
                        }

                        /* Calculate checksum */
                        crc = crc32(MAGIC_SEED, buf, BLOCKSIZE - CHECKSUMSIZE);

                        /* Put checksum into write block */
                        memcpy(buf + BLOCKSIZE - CHECKSUMSIZE, (void*)&crc, CHECKSUMSIZE);

                        /* Write back new data */
			if (crfspwrite(fd[k], buf, BLOCKSIZE, i*BLOCKSIZE) != BLOCKSIZE) {
				printf("File data block write fail \n");
				return NULL;
			}

#if 0
			/* Write back new data */
			if (crfspwrite(fd[k], buf, BLOCKSIZE - CHECKSUMSIZE, i*BLOCKSIZE) != BLOCKSIZE - CHECKSUMSIZE) {
				printf("File data block write fail \n");
				return NULL;
			}

			/* Write back checksum */
			if (crfspwrite(fd[k], buf + BLOCKSIZE - CHECKSUMSIZE, CHECKSUMSIZE, i*BLOCKSIZE + BLOCKSIZE - CHECKSUMSIZE) != CHECKSUMSIZE) {
				printf("File data block checksum fail \n");
				return NULL;
			}
#endif

                        /* gettimeofday(&end_tt, NULL); */
                        /* sec = simulation_time_us(start_tt, end_tt); */
                        /* printf("%.lf\n", sec); */
                }
        }
        gettimeofday(&end_t, NULL);

        sec = simulation_time(start_t, end_t);
        thruput = (double)(file_num_per_thd * (end - start) * BLOCKSIZE) / sec;

        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);
        for (k = 0; k < file_num_per_thd; k++) {
                crfsclose(fd[k]);
        }

        free(buf);
        free(fd);
}

void* do_seq_read(void* arg) {
        char* buf = malloc(BLOCKSIZE);
        uint64_t i = 0, j = 0;
        uint64_t k = 0;
        int thread_id = *(int*)arg;
        uint64_t start = 0;
        uint64_t end = ITERS;
        int block_size = 0;
        uint32_t crc = 0;
        uint32_t actual = 0;
        double sec = 0.0;
        double thruput = 0;
        struct timeval start_t, end_t;
        char test_file[256];
        int file_num_per_thd = 1;
        int file_num_start = 0;
        int file_num_end = 1;
        int* fd = (int *)malloc(512 * sizeof(int));
        int round = 2;

        if (filenum >= thread_nr) {
                file_num_per_thd = (filenum / thread_nr);
                file_num_start = thread_id * file_num_per_thd;
                file_num_end = (thread_id + 1) * file_num_per_thd;
                start = 0;
                end = ITERS;
        }

        for (i = file_num_start, k = 0; i < file_num_end; i++, k++) {
                snprintf(test_file, 256, "%s/testfile%ld", TESTDIR, i);
#ifndef _POSIX
                if ((fd[k] = crfsopen(test_file, O_RDWR, FILEPERM)) < 0) {
#else
                if ((fd[k] = open(test_file, O_RDWR, FILEPERM)) < 0) {
#endif
                        perror("creat");
                        return NULL;
                }
                /* printf("thread_id: %d, file: %s, fd: %d\n", thread_id, test_file, fd[k]); */
        }

        gettimeofday(&start_t, NULL);
#ifdef CRFS_SEQ
        for (int q = 0; q < round; q++) {
#endif
        for (k = 0; k < file_num_per_thd; k++) {

            for (i = start; i < end; i++) {
                //j = rand() % (end + 1 - start) + start;
                /* j = getRandom(0, (FILESIZE - 1024 * BLOCKSIZE )); */
                j = i;
                //block_size = rand() % BLOCKSIZE;
                block_size = BLOCKSIZE;
                /* Read entire block */
                
#ifndef _POSIX
                if (crfspread(fd[k], buf, block_size, j*block_size) != block_size) {
#else
                if (pread(fd[k], buf, block_size, j*block_size) != block_size) {
#endif
                    /* printf("File data block read fail, offset: %ld, block_size: %ld\n", j, block_size); */
                    /* return NULL; */
                }
            }
        }
#ifdef CRFS_SEQ
        }
#endif

        /* printf("file_num_per_thd: %ld, end: %ld, start: %d\n", file_num_per_thd, end, start); */
        gettimeofday(&end_t, NULL);
        sec = simulation_time(start_t, end_t);
        thruput = (double)(file_num_per_thd * (end - start) * BLOCKSIZE) / sec;
        //thruput = (double)((end - start)*1.0 * BLOCKSIZE) / sec;

        /* printf("# of pread: %ld, thruput: %lf \n", end - start, thruput);  */
        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);

#if 1
        for (k = 0; k < file_num_per_thd; k++) {
#ifndef _POSIX
                crfsclose(fd[k]);
#else
                close(fd[k]);
#endif
        }
#endif 

        free(buf);
        free(fd);
}

void* do_seq_write(void* arg) {
        char* buf = malloc(BLOCKSIZE);
        uint64_t i = 0, j = 0;
        uint64_t k = 0;
        int thread_id = *(int*)arg;
        uint64_t start = 0;
        uint64_t end = ITERS;
        int block_size = 0;
        uint32_t crc = 0;
        uint32_t actual = 0;
        double sec = 0.0;
        double thruput = 0;
        struct timeval start_t, end_t;
        char test_file[256];
        int file_num_per_thd = 1;
        int file_num_start = 0;
        int file_num_end = 1;
        int* fd = (int *)malloc(512 * sizeof(int));
        int round = 4;

        if (filenum >= thread_nr) {
                file_num_per_thd = (filenum / thread_nr);
                file_num_start = thread_id * file_num_per_thd;
                file_num_end = (thread_id + 1) * file_num_per_thd;
                start = 0;
                end = ITERS;
        }

        for (i = file_num_start, k = 0; i < file_num_end; i++, k++) {
                snprintf(test_file, 256, "%s/testfile%ld", TESTDIR,
                         i);
#ifndef _POSIX
                if ((fd[k] = crfsopen(test_file, O_RDWR, FILEPERM)) < 0) {
#else
                if ((fd[k] = open(test_file, O_RDWR, FILEPERM)) < 0) {
#endif
                        perror("creat");
                        return NULL;
                }
                /* printf("thread_id: %d, file: %s, fd: %d\n", thread_id, test_file, fd[k]); */
        }

        gettimeofday(&start_t, NULL);
#ifdef CRFS_SEQ
        for (int q=0;q<round;q++) {
#endif
        for (k = 0; k < file_num_per_thd; k++) {

            for (i = start; i < end; i++) {
                //j = rand() % (end + 1 - start) + start;
                /* j = getRandom(0, (FILESIZE - BLOCKSIZE )); */
                j = i;
                //block_size = rand() % BLOCKSIZE;
                block_size = BLOCKSIZE;
                /* Read entire block */
#ifndef _POSIX
                if (crfspwrite(fd[k], buf, block_size, j*block_size) != block_size) {
#else
                if (pwrite(fd[k], buf, block_size, j*block_size) != block_size) {
#endif
                    printf("File data block read fail \n");
                    return NULL;
                }
            }
        }
#ifdef CRFS_SEQ
        }
#endif

        gettimeofday(&end_t, NULL);
        sec = simulation_time(start_t, end_t);
        thruput = (double)(file_num_per_thd * (end - start) * BLOCKSIZE) / sec;
        //thruput = (double)((end - start)*1.0 * BLOCKSIZE) / sec;

        /* printf("# of pwrite: %ld \n", end - start);  */
        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);

#if 1
        for (k = 0; k < file_num_per_thd; k++) {
#ifndef _POSIX
                crfsclose(fd[k]);
#else
                close(fd[k]);
#endif
        }
#endif

        free(buf);
        free(fd);
}

void* do_rand_read(void* arg) {
        char* buf = malloc(BLOCKSIZE);
        uint64_t i = 0, j = 0;
        uint64_t k = 0;
        int thread_id = *(int*)arg;
        uint64_t start = 0;
        uint64_t end = ITERS;
        int block_size = 0;
        uint32_t crc = 0;
        uint32_t actual = 0;
        double sec = 0.0;
        double thruput = 0;
        struct timeval start_t, end_t;
        char test_file[256];
        int file_num_per_thd = 1;
        int file_num_start = 0;
        int file_num_end = 1;
        int* fd = (int *)malloc(512 * sizeof(int));

        if (filenum >= thread_nr) {
                file_num_per_thd = (filenum / thread_nr);
                file_num_start = thread_id * file_num_per_thd;
                file_num_end = (thread_id + 1) * file_num_per_thd;
                start = 0;
                end = ITERS;
        }

        for (i = file_num_start, k = 0; i < file_num_end; i++, k++) {
                snprintf(test_file, 256, "%s/testfile%ld", TESTDIR, i);
#ifndef _POSIX
                if ((fd[k] = crfsopen(test_file, O_RDWR, FILEPERM)) < 0) {
#else
                if ((fd[k] = open(test_file, O_RDWR, FILEPERM)) < 0) {
#endif
                        perror("creat");
                        return NULL;
                }
                /* printf("thread_id: %d, file: %s, fd: %d\n", thread_id, test_file, fd[k]); */
        }

        gettimeofday(&start_t, NULL);
        for (k = 0; k < file_num_per_thd; k++) {

            for (i = start; i < end; i++) {
                //j = rand() % (end + 1 - start) + start;
                j = getRandom(0, (FILESIZE - 1024 * BLOCKSIZE ));
                //block_size = rand() % BLOCKSIZE;
                block_size = BLOCKSIZE;
                /* Read entire block */
                
#ifndef _POSIX
                if (crfspread(fd[k], buf, block_size, j) != block_size) {
#else
                if (pread(fd[k], buf, block_size, j) != block_size) {
#endif
                    /* printf("File data block read fail, offset: %ld, block_size: %ld\n", j, block_size); */
                    /* return NULL; */
                }
            }
        }

        /* printf("file_num_per_thd: %ld, end: %ld, start: %d\n", file_num_per_thd, end, start); */
        gettimeofday(&end_t, NULL);
        sec = simulation_time(start_t, end_t);
        thruput = (double)(file_num_per_thd * (end - start) * BLOCKSIZE) / sec;
        //thruput = (double)((end - start)*1.0 * BLOCKSIZE) / sec;

        /* printf("# of pread: %ld, thruput: %lf \n", end - start, thruput);  */
        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);

#if 1
        for (k = 0; k < file_num_per_thd; k++) {
#ifndef _POSIX
                crfsclose(fd[k]);
#else
                close(fd[k]);
#endif
        }
#endif 

        free(buf);
        free(fd);
}

void* do_rand_write(void* arg) {
        char* buf = malloc(BLOCKSIZE);
        uint64_t i = 0, j = 0;
        uint64_t k = 0;
        int thread_id = *(int*)arg;
        uint64_t start = 0;
        uint64_t end = ITERS;
        int block_size = 0;
        uint32_t crc = 0;
        uint32_t actual = 0;
        double sec = 0.0;
        double thruput = 0;
        struct timeval start_t, end_t;
        char test_file[256];
        int file_num_per_thd = 1;
        int file_num_start = 0;
        int file_num_end = 1;
        int* fd = (int *)malloc(512 * sizeof(int));

        if (filenum >= thread_nr) {
                file_num_per_thd = (filenum / thread_nr);
                file_num_start = thread_id * file_num_per_thd;
                file_num_end = (thread_id + 1) * file_num_per_thd;
                start = 0;
                end = ITERS;
        }

        for (i = file_num_start, k = 0; i < file_num_end; i++, k++) {
                snprintf(test_file, 256, "%s/testfile%ld", TESTDIR,
                         i);
#ifndef _POSIX
                if ((fd[k] = crfsopen(test_file, O_RDWR, FILEPERM)) < 0) {
#else
                if ((fd[k] = open(test_file, O_RDWR, FILEPERM)) < 0) {
#endif
                        perror("creat");
                        return NULL;
                }
                /* printf("thread_id: %d, file: %s, fd: %d\n", thread_id, test_file, fd[k]); */
        }

        gettimeofday(&start_t, NULL);
        for (k = 0; k < file_num_per_thd; k++) {

            for (i = start; i < end; i++) {
                //j = rand() % (end + 1 - start) + start;
                j = getRandom(0, (FILESIZE - BLOCKSIZE ));
                //block_size = rand() % BLOCKSIZE;
                block_size = BLOCKSIZE;
                /* Read entire block */
#ifndef _POSIX
                if (crfspwrite(fd[k], buf, block_size, j) != block_size) {
#else
                if (pwrite(fd[k], buf, block_size, j) != block_size) {
#endif
                    printf("File data block read fail \n");
                    return NULL;
                }
            }
        }

        gettimeofday(&end_t, NULL);
        sec = simulation_time(start_t, end_t);
        thruput = (double)(file_num_per_thd * (end - start) * BLOCKSIZE) / sec;
        //thruput = (double)((end - start)*1.0 * BLOCKSIZE) / sec;

        /* printf("# of pwrite: %ld \n", end - start);  */
        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);

#if 1
        for (k = 0; k < file_num_per_thd; k++) {
#ifndef _POSIX
                crfsclose(fd[k]);
#else
                close(fd[k]);
#endif
        }
#endif

        free(buf);
        free(fd);
}

void *do_read_knn_write(void *arg) {

    printf("unsupported yet!\n");
}
#else

void *do_seq_write(void *arg) {
        printf("unsupported yet!\n");
}

void *do_seq_read(void *arg) {
        printf("unsupported yet!\n");
}

void *do_rand_write(void *arg) {
        printf("unsupported yet!\n");
}

void *do_rand_read(void *arg) {
        printf("unsupported yet!\n");
}

void *do_append_checksum_write(void *arg) {
        char *buf = malloc(BLOCKSIZE);
        uint64_t i = 0;
        uint64_t k = 0;
        int thread_id = *(int *)arg;
        int range = ITERS / thread_nr;
        double start = thread_id * range;
        double end = (thread_id + 1) * range;
        double sec = 0.0;
        double thruput = 0;
        char test_file[256];
        int file_num_per_thd = 1;
        int file_num_start = 0;
        int file_num_end = 1;
        int* fd = (int *)malloc(512 * sizeof(int));

        if (filenum >= thread_nr) {
                file_num_per_thd = (filenum / thread_nr);
                file_num_start = thread_id * file_num_per_thd;
                file_num_end = (thread_id + 1) * file_num_per_thd;
                start = 0;
                end = ITERS;
        }

        /* printf("filenum: %d, thread_nr: %d, thread_id: %d, iters: %ld\n", filenum, thread_nr, thread_id, ITERS); */
        
        struct timeval start_t, end_t;

        for (i = file_num_start, k = 0; i < file_num_end; i++, k++) {
                snprintf(test_file, 256, "%s/testfile%ld", TESTDIR,i);
#ifndef _POSIX
                if ((fd[k] = crfsopen(test_file, O_RDWR, FILEPERM)) < 0) {
#else
                if ((fd[k] = open(test_file, O_RDWR, FILEPERM)) < 0) {
#endif
                        perror("creat");
                        return NULL;
                }
                /* printf("thread_id: %d, file: %s, fd: %d\n", thread_id, test_file, fd[k]); */
        }

        gettimeofday(&start_t, NULL);
        for (k = 0; k < file_num_per_thd; k++) {
                for (i = start; i < end; i++) {
                        /* Write new content to this block */
                        for (int j = 0; j < BLOCKSIZE; j++) {
                                buf[j] = 0x41 + j % 26;
                        }
                        /* Issue a checksum pwrite directly, offload to storage
                         */
#ifdef NO_CACHE
                         if (devfschecksumpwrite(fd[k], buf, BLOCKSIZE, i*BLOCKSIZE) != BLOCKSIZE) {
#else

#ifdef HYBRID_CACHE
                        if (devfsappendchksmpwrite_cache_hybrid(fd[k], buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
#else

#ifdef KERNEL_CACHE
                        if (devfsappendchksmpwrite_cache_kernel(fd[k], buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
#else
                        if (devfschecksumpwrite_cache_host(fd[k], buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
#endif // KENREL_CACHE

#endif // HYBIRD_CACHE

#endif // NO_CACHE
                                printf(
                                    "File data block checksum write fail \n");
                                return NULL;
                        }
                }
        }
        gettimeofday(&end_t, NULL);

        sec = simulation_time(start_t, end_t);
        thruput = (double)(file_num_per_thd * (end - start) * BLOCKSIZE) / sec;

        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);
        for (k = 0; k < file_num_per_thd; k++) {

#ifndef _POSIX
                crfsclose(fd[k]);
#else
                close(fd[k]);
#endif
        }

        free(buf);
        free(fd);
}

void *do_read_knn_write(void *arg) {
        char *buf = malloc(BLOCKSIZE);
        uint64_t i = 0;
        uint64_t k = 0;
        int thread_id = *(int *)arg;
        int range = ITERS / thread_nr;
        double start = thread_id * range;
        double end = (thread_id + 1) * range;
        double sec = 0.0;
        double thruput = 0;
        char test_file[256];
        int file_num_per_thd = 1;
        int file_num_start = 0;
        int file_num_end = 1;
        int* fd = (int *)malloc(512 * sizeof(int));

        if (filenum >= thread_nr) {
                file_num_per_thd = (filenum / thread_nr);
                file_num_start = thread_id * file_num_per_thd;
                file_num_end = (thread_id + 1) * file_num_per_thd;
                start = 0;
                end = ITERS;
        }

        /* printf("filenum: %d, thread_nr: %d, thread_id: %d, iters: %ld\n", filenum, thread_nr, thread_id, ITERS); */
        
        struct timeval start_t, end_t;
        struct timeval start_tt, end_tt;

        for (i = file_num_start, k = 0; i < file_num_end; i++, k++) {
                snprintf(test_file, 256, "%s/testfile%ld", TESTDIR, i);
                if ((fd[k] = crfsopen(test_file, O_RDWR, FILEPERM)) < 0) {
                        perror("creat");
                        return NULL;
                }
                /* printf("thread_id: %d, file: %s, fd: %d\n", thread_id, test_file, fd[k]); */
        }

        gettimeofday(&start_t, NULL);
        for (k = 0; k < file_num_per_thd; k++) {
            devfs_readknnwrite(fd[k], NULL, 0, 0);
        }
        gettimeofday(&end_t, NULL);

        sec = simulation_time(start_t, end_t);
        thruput = (double)(file_num_per_thd * (end - start) * BLOCKSIZE) / sec;

        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);
        for (k = 0; k < file_num_per_thd; k++) {
                crfsclose(fd[k]);
        }

        free(buf);
        free(fd);
}

void *do_read_checksum_write(void *arg) {
        char *buf = malloc(BLOCKSIZE);
        uint64_t i = 0;
        uint64_t k = 0;
        int thread_id = *(int *)arg;
        int range = ITERS / thread_nr;
        double start = thread_id * range;
        double end = (thread_id + 1) * range;
        double sec = 0.0;
        double thruput = 0;
        char test_file[256];
        int file_num_per_thd = 1;
        int file_num_start = 0;
        int file_num_end = 1;
        int* fd = (int *)malloc(512 * sizeof(int));

        if (filenum >= thread_nr) {
                file_num_per_thd = (filenum / thread_nr);
                file_num_start = thread_id * file_num_per_thd;
                file_num_end = (thread_id + 1) * file_num_per_thd;
                start = 0;
                end = ITERS;
        }

        /* printf("filenum: %d, thread_nr: %d, thread_id: %d, iters: %ld\n", filenum, thread_nr, thread_id, ITERS); */
        
        struct timeval start_t, end_t;
        struct timeval start_tt, end_tt;

        for (i = file_num_start, k = 0; i < file_num_end; i++, k++) {
                snprintf(test_file, 256, "%s/testfile%ld", TESTDIR, i);
                if ((fd[k] = crfsopen(test_file, O_RDWR, FILEPERM)) < 0) {
                        perror("creat");
                        return NULL;
                }
                /* printf("thread_id: %d, file: %s, fd: %d\n", thread_id, test_file, fd[k]); */
        }

        gettimeofday(&start_t, NULL);
        for (k = 0; k < file_num_per_thd; k++) {
                for (i = start; i < end; i++) {
                        /* Write new content to this block */
                        for (int j = 0; j < BLOCKSIZE; j++) {
                                buf[j] = 0x41 + j % 26;
                        }
                        /* Issue a checksum pwrite directly, offload to storage
                         */
                        /* printf("devfschecksumpwrite, fd: %d, blocksize: %d, offset: %ld, k: %d, file_num_per_thd: %d\n", fd[k], BLOCKSIZE, i*BLOCKSIZE, k, file_num_per_thd); */
                        /* gettimeofday(&start_tt, NULL); */
#ifdef NO_CACHE
                         if (devfsreadchksmpwrite(fd[k], buf, BLOCKSIZE, i*BLOCKSIZE) != BLOCKSIZE) {
#else

#ifdef HYBRID_CACHE
                        if (devfsreadchksmpwrite_cache_hybrid(fd[k], buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
#else

#ifdef KERNEL_CACHE
                        if (devfsreadchksmpwrite_cache_kernel(fd[k], buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
#else
                        if (devfsreadchksmpwrite_cache_host(fd[k], buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
#endif // KENREL_CACHE

#endif // HYBIRD_CACHE

#endif // NO_CACHE
                                printf(
                                    "File data block checksum write fail \n");
                                return NULL;
                        }
                        /* gettimeofday(&end_tt, NULL); */
                        /* sec = simulation_time_us(start_tt, end_tt); */
                        /* printf("%.lf\n", sec); */
                }
        }
        gettimeofday(&end_t, NULL);

        sec = simulation_time(start_t, end_t);
        thruput = (double)(file_num_per_thd * (end - start) * BLOCKSIZE) / sec;

        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);
        for (k = 0; k < file_num_per_thd; k++) {
                crfsclose(fd[k]);
        }

        free(buf);
        free(fd);
}

#endif

int main(int argc, char **argv) {
        uint64_t i = 0;
        int fd = 0, ret = 0;
        uint32_t crc = 0;
        struct stat st;
        struct timeval start, end;
        double sec = 0.0;
        char *buf;
        char test_file[256];
        char read_dir[256];
        int benchmark_type = 0;
        int fds[256];
        srand((unsigned)time(NULL));

        if (argc < 6) {
                printf("invalid argument\n");
                printf(
                    "./test_checksum benchmark_type(0, 1, 2...) IO_size "
                    "thread_count file_size (MB) file_num\n");
                return 0;
        }
        /* Get benchmark type from input argument */
        benchmark_type = atoi(argv[1]);
        switch (benchmark_type) {
                case 0:
                        benchmark = &do_rand_read;
                        break;
                case 1:
                        benchmark = &do_rand_write;
                        break;
                case 2:
                        benchmark = &do_seq_read;
                        break;
                case 3:
                        benchmark = &do_seq_write;
                        break;
                case 4:  
                        benchmark = &do_read_knn_write;
                        break;
                case 5:  
                        benchmark = &do_read_checksum_write;
                        break;
                default:
                        benchmark = &do_append_checksum_write;
                        break;
        }

        /* Get I/O size (value size) from input argument */
        BLOCKSIZE = atoi(argv[2]);
        thread_nr = atoi(argv[3]);
        FILESIZE = atoi(argv[4]) * MB;
        filenum = atoi(argv[5]);

        buf = (char *)malloc(BLOCKSIZE);
        ITERS = FILESIZE / BLOCKSIZE;
#ifndef _POSIX
#ifdef CXL_NAMESPACE
        // allocate the namespace based on the filenum
        crfsinit_cxl_ns(USE_DEFAULT_PARAM, USE_DEFAULT_PARAM,
                 DEFAULT_SCHEDULER_POLICY, filenum);
#else
        crfsinit(USE_DEFAULT_PARAM, USE_DEFAULT_PARAM,
                 DEFAULT_SCHEDULER_POLICY);
#endif
#endif

        gettimeofday(&start, NULL);
        for (int k = 0; k < filenum; k++) {
                snprintf(test_file, 256, "%s/testfile%d", TESTDIR,
                         k);
                /* printf("create new file: %s, size: %ld\n", test_file, FILESIZE); */

#ifndef _POSIX
                /* Step 1: Create Testing File */
                if ((fds[k] = crfsopen(test_file, O_CREAT | O_RDWR, FILEPERM)) <
                    0) {
#else
                if ((fds[k] = open(test_file, O_CREAT | O_RDWR, FILEPERM)) < 0) {
#endif
                        perror("creat");
                        goto benchmark_exit;
                }

                /* Step 2: Append 1M blocks to storage with random contents and
                 * checksum */
                for (i = 0; i < ITERS; i++) {
                        /* memset with some random data */
                        memset(buf, 0x61 + i % 26, BLOCKSIZE);
#ifdef  _POSIX

                         if (write(fds[k], buf, BLOCKSIZE) != BLOCKSIZE) {
#else 

#ifdef CRFS_POSIX
                         if (crfswrite(fds[k], buf, BLOCKSIZE) != BLOCKSIZE) {
#else

#ifdef NO_CACHE
                         if (devfschecksumpwrite(fds[k], buf, BLOCKSIZE, i*BLOCKSIZE) != BLOCKSIZE) {
#else

#ifdef HYBRID_CACHE
                         if (crfswrite(fds[k], buf, BLOCKSIZE) != BLOCKSIZE) {
                        //if (devfsappendchksmpwrite_cache_hybrid(fd, buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
#else

#ifdef KERNEL_CACHE
                        if (devfsappendchksmpwrite_cache_kernel(fds[k], buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
#else
                        if (devfschecksumpwrite_cache_host(fds[k], buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
#endif // KENREL_CACHE

#endif // HYBIRD_CACHE

#endif // NO_CACH

#endif // CRFS_POSIX
#endif // POSIX
                                printf("File data block write fail \n");
                                goto benchmark_exit;
                        }
                }
                //printf("finished writing\n");
        }
        gettimeofday(&end, NULL);
        sec = simulation_time(start, end);
        /* printf("finished all writing, sec: %.2lf s\n", sec); */

        tid = malloc(thread_nr * sizeof(pthread_t));
        thread_idx = malloc(thread_nr * sizeof(int));

        pthread_mutex_init(&g_lock, NULL);

        /* Start timing checksum write */
        gettimeofday(&start, NULL);

        /* Step 3: Run benchmark */
        for (i = 0; i < thread_nr; ++i) {
                thread_idx[i] = i;
                pthread_create(&tid[i], NULL, benchmark, &thread_idx[i]);
        }

        for (i = 0; i < thread_nr; ++i) pthread_join(tid[i], NULL);

        gettimeofday(&end, NULL);
        sec = simulation_time(start, end);
        /* printf("Benchmark takes %.2lf s, average thruput %lf B/s\n", sec, */
               /* g_avgthput); */
        printf("Benchmark takes %.2lf s, average thruput %.2lf GB/s\n", sec,
               g_avgthput / 1024 / 1024 / 1024);
        printf("Benchmark completed \n");

benchmark_exit:
        free(tid);
        free(thread_idx);
        free(buf);

#ifndef _POSIX
        for (int k = 0; k < filenum; k++) {
                crfsclose(fds[k]);
        }
        crfsexit();
#else
        close(fd);
#endif

        return 0;
}
