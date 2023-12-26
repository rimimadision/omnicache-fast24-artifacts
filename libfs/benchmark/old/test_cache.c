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

int thread_nr = 0;

int filenum = 0;
int BLOCKSIZE = 4096;
int ITERS = 0;
unsigned long FILESIZE = 1 * GB;
unsigned long real_FILESIZE = 0;

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

void* do_seq_read(void* arg) {
        char* buf = malloc(BLOCKSIZE);
        uint64_t i = 0, j = 0;
        uint64_t k = 0;
        int thread_id = *(int*)arg;
        int range = ITERS / thread_nr1;
        double start = thread_id * range;
        double end = (thread_id + 1) * range;
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
                snprintf(test_file, 256, "%s/testfile%d", TESTDIR,
                         k);
                if ((fd[k] = crfsopen(test_file, O_RDWR, FILEPERM)) < 0) {
                        perror("creat");
                        return NULL;
                }
                /* printf("thread_id: %d, file: %s, fd: %d\n", thread_id, test_file, fd[k]); */
        }


        gettimeofday(&start_t, NULL);

        for (k = 0; k < file_num_per_thd; k++) {
            for (i = start; i < end; i++) {
                /* Read entire block */
                if (crfspread(fd[k], buf, BLOCKSIZE, i * BLOCKSIZE) != BLOCKSIZE) {
                    printf("File data block read fail \n");
                    return NULL;
                }

            }
        }

        gettimeofday(&end_t, NULL);
        sec = simulation_time(start_t, end_t);
        thruput = (double)(file_num_per_thd * (end - start) * BLOCKSIZE) / sec;
        /* thruput = (double)((end - start) * BLOCKSIZE) / sec; */

        pthread_mutex_lock(&g_lock);
        g_avgthput += thruput;
        pthread_mutex_unlock(&g_lock);

        for (k = 0; k < file_num_per_thd; k++) {
                crfsclose(fd[k]);
        }

        free(buf);
        free(fd);
}

void* do_rand_read(void* arg) {
        char* buf = malloc(BLOCKSIZE);
        uint64_t i = 0, j = 0;
        int fd = 0;
        int thread_id = *(int*)arg;
        int start = 0;
        int end = ITERS;
        int block_size = 0;
        uint32_t crc = 0;
        uint32_t actual = 0;
        double sec = 0.0;
        double thruput = 0;
        int ret = 0;
        struct timeval start_t, end_t;

        if ((fd = crfsopen(TESTDIR "/testfile", O_RDWR, FILEPERM)) < 0) { 
                perror("creat");
                return NULL;
        }
        gettimeofday(&start_t, NULL);
        for (i = start; i < end; i++) {
                j = rand() % (FILESIZE - BLOCKSIZE );
                //block_size = rand() % BLOCKSIZE;
                block_size = BLOCKSIZE;
                /* Read entire block */
                if ((ret = crfspread(fd, buf, block_size, j)) != block_size) {
                        printf("File data block read fail, offset: %ld, ret: %ld \n", j, ret);
                        exit(-1);
                        return NULL;
                }
        }

        gettimeofday(&end_t, NULL);
        sec = simulation_time(start_t, end_t);
        thruput = (double)((end - start)*1.0 * BLOCKSIZE) / sec;
        printf("# of pread: %d \n", end - start); 
        /* printf("end: %d, start: %d, sec: %f, thruput: %lf \n", start, end, sec, thruput); */
        pthread_mutex_lock(&g_lock);
        g_avgthput_reader += thruput;
        pthread_mutex_unlock(&g_lock);

        crfsclose(fd);
        free(buf);
}

void* do_rand_write(void* arg) {
        char* buf = malloc(BLOCKSIZE);
        uint64_t i = 0, j = 0;
        int fd = 0;
        int thread_id = *(int*)arg;
        int start = 0;
        int end = ITERS;
        int block_size = 0;
        uint32_t crc = 0;
        uint32_t actual = 0;
        double sec = 0.0;
        double thruput = 0;
        struct timeval start_t, end_t;
        if ((fd = crfsopen(TESTDIR "/testfile", O_RDWR, FILEPERM)) < 0) {
                perror("creat");
                return NULL;
        }
        gettimeofday(&start_t, NULL);
        for (i = start; i < end; i++) {
                //j = rand() % (end + 1 - start) + start;
                j = rand() % (FILESIZE - BLOCKSIZE );
                //block_size = rand() % BLOCKSIZE;
                block_size = BLOCKSIZE;
                if (block_size == 0) block_size = BLOCKSIZE;
                /* Read entire block */
                if (crfspwrite(fd, buf, block_size, j) != block_size) {
                        printf("File data block read fail \n");
                        return NULL;
                }
        }

        gettimeofday(&end_t, NULL);
        sec = simulation_time(start_t, end_t);
        thruput = (double)((end - start)*1.0 * BLOCKSIZE) / sec;

        printf("# of pwrite: %d \n", end - start); 
        pthread_mutex_lock(&g_lock);
        g_avgthput_writer += thruput;
        pthread_mutex_unlock(&g_lock);

        crfsclose(fd);
        free(buf);
}

int main(int argc, char** argv) {
        uint64_t i = 0, j = 0;
        int fd = 0, ret = 0;
        uint32_t crc = 0;
        struct stat st;
        struct timeval start, end;
        double sec = 0.0;
        char* buf;
        int bench_type = 0;
        int block_size = 0;
        unsigned long offset = 0;
        char test_file[256];

        if (argc < 5) {
                printf("invalid argument\n");
                printf(
                    "./test_checksum benchmark_type(1, 2, 3 or 4) IO_size filenum "
                    "thread_count <file_size (MB)>\n");
                return 0;
        }

        srand((unsigned)time(NULL));
        bench_type = atoi(argv[1]);
        /* Get benchmark type from input argument */
        switch (bench_type) {
                case 1:  
                        benchmark1 = &do_seq_read;
                        break;
                case 2:  
                        benchmark1 = &do_rand_write;
                        break;
                case 3:  
                        benchmark1 = &do_rand_read;
                        break;
                case 4:
                        benchmark1 = &do_rand_write;
                        benchmark2 = &do_rand_read;
                        break;
                default:
                        benchmark1 = &do_seq_read;
                        break;
        }

        /* Get I/O size (value size) from input argument */
        BLOCKSIZE = atoi(argv[2]);
        filenum = atoi(argv[3]);
        thread_nr1 = atoi(argv[4]);
        if (argc >= 6) {
                thread_nr2 = atoi(argv[5]);
        }

        thread_nr = thread_nr1;

        buf = (char*)malloc(BLOCKSIZE);
        ITERS = FILESIZE / BLOCKSIZE;

        crfsinit(USE_DEFAULT_PARAM, USE_DEFAULT_PARAM, DEFAULT_SCHEDULER_POLICY);

        for (int k = 0; k < filenum; k++) {
                snprintf(test_file, 256, "%s/testfile%d", TESTDIR,
                         k);
                printf("create new file: %s, size: %ld\n", test_file, FILESIZE);

                if ((fd = crfsopen(test_file, O_CREAT | O_RDWR, FILEPERM)) < 0) {
                        goto benchmark_exit;
                }

                for (i = 0; i < ITERS; i++) {
                        /* memset with some random data */
                        memset(buf, 0x61 + i % 26, BLOCKSIZE);
                        if (crfswrite(fd, buf, block_size) != block_size) {
                            printf("File data block write fail \n");
                            goto benchmark_exit;
                        }
                        
                }
        }

        printf("finish writing %lu files\n", filenum);

        tid1 = malloc(thread_nr1 * sizeof(pthread_t));
        thread_idx1 = malloc(thread_nr1 * sizeof(int));
        if (bench_type == PARALLEL_BENCH) {
                tid2 = malloc(thread_nr2 * sizeof(pthread_t));
                thread_idx2 = malloc(thread_nr2 * sizeof(int));
        }

        pthread_mutex_init(&g_lock, NULL);

        /* Start timing checksum write */
        gettimeofday(&start, NULL);

        /* Step 3: Run benchmark */
        for (i = 0; i < thread_nr1; ++i) {
                thread_idx1[i] = i;
                pthread_create(&tid1[i], NULL, benchmark1, &thread_idx1[i]);
        }

        if (bench_type == PARALLEL_BENCH) {
                for (i = 0; i < thread_nr2; ++i) {
                        thread_idx2[i] = i;
                        pthread_create(&tid2[i], NULL, benchmark2, &thread_idx2[i]);
                }
        }

        if (bench_type == PARALLEL_BENCH) {
                for (i = 0; i < thread_nr1; ++i) {
                        pthread_join(tid1[i], NULL);
                }
                for (i = 0; i < thread_nr2; ++i) {
                        pthread_join(tid2[i], NULL);
                }
        } else {
                for (i = 0; i < thread_nr1; ++i) pthread_join(tid1[i], NULL);
        }


        gettimeofday(&end, NULL);
        sec = simulation_time(start, end);
        printf("Reader Benchmark takes %.2lf s, average thruput %.2lf GB/s\n", sec,
               g_avgthput_reader / 1024 / 1024 / 1024);

        printf("Writer Benchmark takes %.2lf s, average thruput %.2lf GB/s\n", sec,
               g_avgthput_writer / 1024 / 1024 / 1024);


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
