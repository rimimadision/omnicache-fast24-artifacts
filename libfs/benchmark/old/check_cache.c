

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
#else
#define TESTDIR "/mnt/ram"
#endif

#define KB (1024UL)
#define MB (1024 * KB)
#define GB (1024 * MB)
#define FILEPERM 0666

#define CHECKSUMSIZE 4
#define MAGIC_SEED 0

/*
 * By default, thread # is fixed to 4
 * and block size (a.k.a value size) is 4096
 */
int thread_nr1 = 4;
int thread_nr2 = 4;
int BLOCKSIZE = (4 * 4096);
int ITERS = 0;
unsigned long FILESIZE = 4 * GB;
//unsigned long FILESIZE = 100 * MB;

pthread_mutex_t g_lock;
double g_avgthput = 0;
double g_avgthput_reader = 0;
double g_avgthput_writer = 0;

pthread_t* tid1;
int* thread_idx1;
pthread_t* tid2;
int* thread_idx2;

int PARALLEL_BENCH = 4;

/* benchmark function pointer */
void* (*benchmark1)(void*) = NULL;
void* (*benchmark2)(void*) = NULL;

/* To calculate simulation time */
double simulation_time(struct timeval start, struct timeval end) {
        double current_time;
        current_time = ((end.tv_sec + end.tv_usec * 1.0 / 1000000) -
                        (start.tv_sec + start.tv_usec * 1.0 / 1000000));
        return current_time;
}

int main(int argc, char** argv) {
        uint64_t i = 0, j = 0;
        int fd = 0, ret = 0;
        uint32_t crc = 0;
        struct stat st;
        struct timeval start, end;
        double sec = 0.0;
        char* buf;
        char* check_buf;
        int bench_type = 0;
        int block_size = 0;
        unsigned long offset = 0;
        unsigned long *offsets = NULL;
        unsigned long *nlbs= NULL;
        unsigned long *crcs= NULL;
        uint32_t actual = 0;
        if (argc < 4) {
                printf("invalid argument\n");
                printf(
                    "./test_checksum benchmark_type(1, 2, 3 or 4) IO_size "
                    "thread_count <file_size (MB)>\n");
                return 0;
        }

        srand((unsigned)time(NULL));
        bench_type = atoi(argv[1]);
        /* Get benchmark type from input argument */

        /* Get I/O size (value size) from input argument */
        //BLOCKSIZE = atoi(argv[2]);
        thread_nr1 = atoi(argv[3]);
        if (argc >= 5) {
                thread_nr2 = atoi(argv[4]);
        }

        buf = (char*)malloc(BLOCKSIZE);
        check_buf = (char*)malloc(BLOCKSIZE);
        ITERS = FILESIZE / BLOCKSIZE;

        crfsinit(USE_DEFAULT_PARAM, USE_DEFAULT_PARAM, USE_DEFAULT_PARAM);

        /* Step 1: Create Testing File */
        if ((fd = crfsopen(TESTDIR "/testfile", O_CREAT | O_RDWR, FILEPERM)) <
            0) {
                perror("creat");
                goto benchmark_exit;
        }

        /* Step 2: Append 1M blocks to storage with random contents and checksum
         */

        offset = 0;
        offsets = (unsigned long *)malloc(ITERS * sizeof(unsigned long));
        nlbs = (unsigned long *)malloc(ITERS * sizeof(unsigned long));
        crcs = (unsigned long *)malloc(ITERS * sizeof(unsigned long));
        for (i = 0; i < ITERS; i++) {
                /* memset with some random data */
                memset(buf, 0x61 + i % 26, BLOCKSIZE);
                /* block_size =  BLOCKSIZE; */
                block_size = rand() % (BLOCKSIZE + 1 - 4096) + 4096;
                /* block_size = BLOCKSIZE; */
                /* offset = rand() % (FILESIZE - block_size); */
                if (block_size == 0 || block_size == 1 ) block_size = BLOCKSIZE;

                actual = crc32(MAGIC_SEED, buf, block_size); 
                if (crfswrite(fd, buf, block_size) != block_size) {
                        printf("File data block write fail \n");
                        goto benchmark_exit;
                }

                if (crfspread(fd, check_buf, block_size, offset) != block_size) {
                        printf("File data block write fail \n");
                        goto benchmark_exit;
                }


                offsets[i] = offset;
                nlbs[i] = block_size;
                crcs[i] = actual;

                offset += block_size;

                crc = crc32(MAGIC_SEED, check_buf, block_size); 
              /* Compare checksum */
                if (crc != actual) {
                        printf("checksum mismatch! actual = %ld, get = %ld\n",
                                        actual, crc);
                        break;
                }
                 memset(check_buf, 0, BLOCKSIZE);
                //offset += block_size;

        }
        printf("finish writing %lu blocks\n", i);
#if 0 
        crfsclose(fd);
        if ((fd = crfsopen(TESTDIR "/testfile", O_CREAT | O_RDWR, FILEPERM)) <
            0) {
                perror("creat");
                goto benchmark_exit;
        }

        for (i = 0; i < ITERS; i++) {
                /* memset with some random data */
                memset(buf, 0, BLOCKSIZE);
                /* block_size =  BLOCKSIZE; */
                block_size = nlbs[i];
                //block_size = BLOCKSIZE;
                offset = offsets[i];

                if (crfspread(fd, buf, block_size, offset) != block_size) {
                        printf("File data block write fail \n");
                        goto benchmark_exit;
                }
                crc = crc32(MAGIC_SEED, buf, block_size);
                actual = crcs[i];

                if (crc != actual) {
                        printf("checksum mismatch2! actual = %d, get = %d\n",
                                        actual, crc);
                        break;
                }
        }

#endif
        tid1 = malloc(thread_nr1 * sizeof(pthread_t));
        thread_idx1 = malloc(thread_nr1 * sizeof(int));
        if (bench_type == PARALLEL_BENCH) {
                tid2 = malloc(thread_nr2 * sizeof(pthread_t));
                thread_idx2 = malloc(thread_nr2 * sizeof(int));
        }

        pthread_mutex_init(&g_lock, NULL);


        printf("Benchmark completed \n");

benchmark_exit:
        free(tid1);
        free(thread_idx1);
        if (bench_type == PARALLEL_BENCH) {
                free(tid2);
                free(thread_idx2);
        }
        
        free(buf);

        crfsclose(fd);
        crfsexit();

        return 0;
}
