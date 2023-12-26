#ifndef CRFSKNN_H
#define CRFSKNN_H
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "knn_const.h"
#include "crfslib.h"

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

unsigned long read_knn_data_buf(int fd, char* buf, unsigned long start_train_idx);

void read_knn_data(int fd, unsigned long start_train_idx, unsigned long count);

void gen_predicting_data(PredictingCase* predicting_cases);

void calc_distance(unsigned int* distance_arr, PredictingCase* predicting_cases, 
        unsigned long start_train_idx, unsigned long count);

void prediction(unsigned int* distance_arr);
#endif

