#ifndef CRFSKNN_H
#define CRFSKNN_H
#include <linux/devfs.h>

//#define NUM_TRAIN_CASES (14680064) // should be the multiples of READ_BUF_SIZE
//#define NUM_TRAIN_CASES (4096) // should be the multiples of READ_BUF_SIZE
#define NUM_TRAIN_CASES (65536)
#define NUM_PREDICTING_CASES (8)
#define FEATURE_DIM (4096)
#define NUM_RESULTS (32)
#define MAX_FEATURE_WEIGHT (10)
#define RESULT_SIZE (64)
#define WEIGHT_SIZE (1)
#define DATA_GEN_NUM_THREADS (16)
#define COMPUTATION_NUM_THREADS (8)
#define BUS_WIDTH (64)
//#define READ_BUF_SIZE (1024 * 1024 * 2)
#define READ_BUF_SIZE (4096)
#define PARAM_K (10)

typedef struct {
    unsigned char result;
    unsigned char feature_vec[FEATURE_DIM];
} TrainCase;

typedef struct {
    unsigned char feature_vec[FEATURE_DIM];
} PredictingCase;

typedef struct {
    unsigned int distance;
    unsigned char result;
} DisPair;

// void read_knn_data(struct crfss_fstruct *rd, nvme_cmdrw_t *cmdrw);

unsigned long read_knn_data_buf(int fd, char* buf, unsigned long start_train_idx);

void gen_predicting_data(PredictingCase* predicting_cases);

void calc_distance(unsigned int* distance_arr, PredictingCase* predicting_cases, unsigned long start_train_idx, unsigned long count);
// void calc_distance(unsigned int* distance_arr, PredictingCase* predicting_cases);

void prediction(unsigned int* distance_arr);

#endif
