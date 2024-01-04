#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "knn_const.h"
#include "data_generator.h"
#include "crfslib.h"
#define TESTDIR "/mnt/ram"
#define FILEPERM 0666

#define KB (1024UL)
#define MB (1024 * KB)
#define GB (1024 * MB)

int thread_nr = 4;
pthread_mutex_t g_lock;
double datasize = 260 * MB;
double g_avgthput = 0;

/* To calculate simulation time */
double simulation_time(struct timeval start, struct timeval end) {
        double current_time;
        current_time = ((end.tv_sec + end.tv_usec * 1.0 / 1000000) -
                        (start.tv_sec + start.tv_usec * 1.0 / 1000000));
        return current_time;
}

/* benchmark function pointer */
void *(*benchmark)(void *) = NULL;

pthread_t *tid;
int *thread_idx;

void *do_read_knn_write(void *arg) {
	int thread_id = *(int*)arg;
	char test_file[256];
    int fd = 0;
    int round = 5;
    struct timeval start, end;
    double sec = 0.0;
    double thruput = 0;


    gettimeofday(&start, NULL);

	snprintf(test_file, 256, "%s/testfile%ld", TESTDIR,
			thread_id);

    fd = crfsopen(test_file, O_RDWR, 0666);

#ifdef _CACHE
    for (int i = 0; i < round;i++)
#endif
        devfs_readknnwrite(fd, NULL,0, 0);

    crfsclose(fd);
	gettimeofday(&end, NULL);
	sec = simulation_time(start, end);

    thruput = (double)(datasize) / sec;

    pthread_mutex_lock(&g_lock);
    g_avgthput += thruput;
    pthread_mutex_unlock(&g_lock);
}

int main(int argc, char **argv) {

    struct timeval start, end;
    double sec = 0.0;
    int fds[256];
    int fd;
    char test_file[256];
    int i =0;
    int fd1;

    thread_nr = atoi(argv[1]);
    /* thread_nr = 4; */

    crfsinit(USE_DEFAULT_PARAM, USE_DEFAULT_PARAM, DEFAULT_SCHEDULER_POLICY);


    for (int k = 0; k < thread_nr; k++) {
        snprintf(test_file, 256, "%s/testfile%d", TESTDIR,
                k);
        fds[k] = crfsopen(test_file, O_CREAT | O_RDWR, FILEPERM);
        /* fd1 = crfsopen("/mnt/ram/knn_data.txt", O_CREAT | O_RDWR, FILEPERM); */

        /* gen_file(fd1); */
        gen_file(fds[k]);

    }    

    gettimeofday(&start, NULL);

	benchmark = &do_read_knn_write;

	tid = malloc(thread_nr * sizeof(pthread_t));
	thread_idx = malloc(thread_nr * sizeof(int));

	pthread_mutex_init(&g_lock, NULL);

	/* Step 3: Run benchmark */
	for (i = 0; i < thread_nr; ++i) {
		thread_idx[i] = i;
		pthread_create(&tid[i], NULL, benchmark, &thread_idx[i]);
	}

	for (i = 0; i < thread_nr; ++i) pthread_join(tid[i], NULL);

	gettimeofday(&end, NULL);
	sec = simulation_time(start, end);

#if 0
    
	snprintf(test_file, 256, "%s/testfile%ld", TESTDIR,
			0);
    int fd2 = crfsopen(test_file, O_RDWR, 0666);

    printf("finish writing\n");


    devfs_readknnwrite(fd2, NULL,0, 0);

    gettimeofday(&end, NULL);
    sec = simulation_time(start, end);
#endif
    printf("Benchmark finished, average thruput %.2lf GB/s\n",
                           ((double)g_avgthput) / 1024 / 1024 / 1024);


    for (int k = 0; k < thread_nr; k++) {
        crfsclose(fds[k]);
    }
 
    /* crfsclose(fd2); */
    crfsexit();


#if 0
#if 1
    for (int k = 0; k < thread_nr; k++) {
        snprintf(test_file, 256, "%s/testfile%d", TESTDIR,
                                         k);
        fds[k] = crfsopen("/mnt/ram/knn_data.txt", O_CREAT | O_RDWR, FILEPERM);

        gen_file(fds[k]);

    }
#endif



	benchmark = &do_read_knn_write;

	tid = malloc(thread_nr * sizeof(pthread_t));
	thread_idx = malloc(thread_nr * sizeof(int));

	pthread_mutex_init(&g_lock, NULL);
#if 0

	/* Step 3: Run benchmark */
	for (i = 0; i < thread_nr; ++i) {
		thread_idx[i] = i;
		pthread_create(&tid[i], NULL, benchmark, &thread_idx[i]);
	}

	for (i = 0; i < thread_nr; ++i) pthread_join(tid[i], NULL);

	gettimeofday(&end, NULL);
	sec = simulation_time(start, end);
#else

	snprintf(test_file, 256, "%s/testfile%ld", TESTDIR,
			0);

    if ((fd = open("/mnt/ram/knn_data.txt", O_RDWR, FILEPERM)) < 0) {
        perror("creat");
        return 0;
    }
    devfs_readknnwrite(fd, NULL,0, 0);
 
    close(fd);
	gettimeofday(&end, NULL);
	sec = simulation_time(start, end);

#endif
	printf("Benchmark takes %.2lf s\n", sec);

#if 1
    free(tid);
    free(thread_idx);
    for (int k = 0; k < thread_nr; k++) {
        crfsclose(fds[k]);
    }
#endif 
	crfsexit();
#endif

	return 0;
}

