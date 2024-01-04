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

#define c2i(c) (c - '0')

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

unsigned long read_knn_data_buf(int fd, char* buf, unsigned long start_train_idx, TrainCase* train_cases);

void read_knn_data(int fd, unsigned long start_train_idx, unsigned long count, TrainCase* train_cases);

void gen_predicting_data(PredictingCase* predicting_cases);

void calc_distance(unsigned int* distance_arr, PredictingCase* predicting_cases, 
        unsigned long start_train_idx, unsigned long count,  TrainCase* train_cases);

void prediction(unsigned int* distance_arr,  TrainCase* train_cases);
#endif

