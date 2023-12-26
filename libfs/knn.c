#include "knn.h"

#define c2i(c) (c - '0')
#define max(a, b) ((a) > (b) ? (a) : (b))

unsigned char result[NUM_TRAIN_CASES];
TrainCase train_cases[NUM_TRAIN_CASES];

//PredictingCase predicting_cases[NUM_PREDICTING_CASES];

//unsigned int distance_arr[NUM_PREDICTING_CASES][NUM_TRAIN_CASES];
unsigned long read_knn_data_buf(int fd, char* buf, unsigned long start_train_idx) {

    //char buf[READ_BUF_SIZE];
    int state = 0;
    int data_idx = 0;
    int feature_idx = 0;
    int ten_exp_table[WEIGHT_SIZE];

    ten_exp_table[0] = 1;
    int i;
    for (i = 1; i < WEIGHT_SIZE; i++) {
        ten_exp_table[i] = ten_exp_table[i - 1] * 10;
    }

    int read_len = 0;
    int tmp_len;

    //while (read_len != READ_BUF_SIZE &&
    //        (tmp_len = crfsread(fd, buf, READ_BUF_SIZE - read_len))) {
    //    printf("crfs read, tmp_len: %ld\n", tmp_len);
    //    read_len += tmp_len;
    //}
    //if (!tmp_len) {
    //    break;
    //}
    int k;
    for (k = 0; k < READ_BUF_SIZE / BUS_WIDTH; k++) {
        char *buf_ptr = buf + k * BUS_WIDTH;
        if (state == 0) {
            // result
            train_cases[data_idx + start_train_idx].result =
                c2i(buf_ptr[BUS_WIDTH - 2]) * 10 + c2i(buf_ptr[BUS_WIDTH - 1]);
            state++;
        } else {
            // feature
            for (i = 0; i < BUS_WIDTH / WEIGHT_SIZE; i++) {
                int buf_idx = i * WEIGHT_SIZE;
                unsigned char weight = 0;
                for (int j = 0; j < WEIGHT_SIZE; j++) {
                    weight +=
                        c2i(buf_ptr[buf_idx + j]) * ten_exp_table[WEIGHT_SIZE - 1 - j];
                }
                train_cases[data_idx + start_train_idx].feature_vec[feature_idx++] = weight;
            }
            if (state == FEATURE_DIM * WEIGHT_SIZE / BUS_WIDTH) {
                //assert(feature_idx == FEATURE_DIM);
                state = 0;
                feature_idx = 0;
                data_idx++;

            } else {
                state++;
            }
        }
    }
    return data_idx;
    //assert(data_idx == NUM_TRAIN_CASES);
}

void read_knn_data(int fd, unsigned long start_train_idx, unsigned long count) {

    char buf[READ_BUF_SIZE];
    int state = 0;
    int data_idx = 0;
    int feature_idx = 0;
    int ten_exp_table[WEIGHT_SIZE];

    ten_exp_table[0] = 1;
    int i;
    for (i = 1; i < WEIGHT_SIZE; i++) {
        ten_exp_table[i] = ten_exp_table[i - 1] * 10;
    }

    while (1) {
        int read_len = 0;
        int tmp_len;
        if (data_idx >= count)
            break;

        while (read_len != READ_BUF_SIZE &&
                (tmp_len = crfsread(fd, buf, READ_BUF_SIZE - read_len))) {
            printf("crfs read, tmp_len: %ld\n", tmp_len);
            read_len += tmp_len;
        }
        if (!tmp_len) {
            break;
        }
        int k;
        for (k = 0; k < READ_BUF_SIZE / BUS_WIDTH; k++) {
            char *buf_ptr = buf + k * BUS_WIDTH;
            if (state == 0) {
                // result
                train_cases[data_idx + start_train_idx].result =
                    c2i(buf_ptr[BUS_WIDTH - 2]) * 10 + c2i(buf_ptr[BUS_WIDTH - 1]);
                state++;
            } else {
                // feature
                for (i = 0; i < BUS_WIDTH / WEIGHT_SIZE; i++) {
                    int buf_idx = i * WEIGHT_SIZE;
                    unsigned char weight = 0;
                    for (int j = 0; j < WEIGHT_SIZE; j++) {
                        weight +=
                            c2i(buf_ptr[buf_idx + j]) * ten_exp_table[WEIGHT_SIZE - 1 - j];
                    }
                    train_cases[data_idx + start_train_idx].feature_vec[feature_idx++] = weight;
                }
                if (state == FEATURE_DIM * WEIGHT_SIZE / BUS_WIDTH) {
                    //assert(feature_idx == FEATURE_DIM);
                    state = 0;
                    feature_idx = 0;
                    data_idx++;
                    
                } else {
                    state++;
                }
            }
        }
    }
    //assert(data_idx == NUM_TRAIN_CASES);
}

int sqr(int x) { return x * x;  }

void calc_distance(unsigned int* distance_arr, PredictingCase* predicting_cases, unsigned long start_train_idx, unsigned long count) {
    int i, j, k;
    for (k = 0; k < FEATURE_DIM; k++) {
        //for (i = 0; i < NUM_TRAIN_CASES; i++) {
        for (i = start_train_idx; i < count; i++) {
            for (j = 0; j < NUM_PREDICTING_CASES; j++) {
                distance_arr[j*NUM_PREDICTING_CASES + i] += sqr(train_cases[i].feature_vec[k] - predicting_cases[j].feature_vec[k]);

            }
        }
    }
}

void gen_predicting_data(PredictingCase* predicting_cases) {
  int i, j;
  for (i = 0; i < NUM_PREDICTING_CASES; i++) {
    for (j = 0; j < FEATURE_DIM; j++) {
      predicting_cases[i].feature_vec[j] = rand() % MAX_FEATURE_WEIGHT;
    }
  }
}

void prediction(unsigned int* distance_arr) {
  int i, j;
    DisPair max_heap[PARAM_K];
    unsigned int* freq_map = malloc(NUM_TRAIN_CASES*sizeof(unsigned int));
  for (i = 0; i < NUM_PREDICTING_CASES; i++) {
    int heap_size = 0;
    memset(freq_map, 0, NUM_TRAIN_CASES*sizeof(unsigned int));
    for (j = 0; j < NUM_TRAIN_CASES; j++) {
      int cur_dis = distance_arr[i * NUM_PREDICTING_CASES + j];
      if (heap_size < PARAM_K) {
        max_heap[heap_size].distance = cur_dis;
        max_heap[heap_size].result = train_cases[j].result;
        heap_size++;
        if (heap_size == PARAM_K) {
          int p;
          for (p = PARAM_K / 2 - 1; p >= 0; p--) {
            int k = p;
            while (2 * k + 1 < PARAM_K) {
              int l = 2 * k + 1;
              if (l + 1 < PARAM_K &&
                  max_heap[l + 1].distance < max_heap[l].distance) {
                l++;
              }
              if (max_heap[k].distance <= max_heap[l].distance) {
                break;
              }
              DisPair temp = max_heap[l];
              max_heap[l] = max_heap[k];
              max_heap[k] = temp;
              k = l;
            }
          }
        }
      } else if (cur_dis < max_heap[0].distance) {
        max_heap[0].distance = cur_dis;
        max_heap[0].result = train_cases[j].result;
        int k = 0;
        while (2 * k + 1 < PARAM_K) {
          int l = 2 * k + 1;
          if (l + 1 < PARAM_K && max_heap[l + 1].distance < max_heap[l].distance) {
            l++;
          }
          if (max_heap[k].distance <= max_heap[l].distance) {
            break;
          }
          DisPair temp = max_heap[l];
          max_heap[l] = max_heap[k];
          max_heap[k] = temp;
          k = l;
        }
      }
    }
    for (j = 0; j < PARAM_K; j++) {
      freq_map[max_heap[j].result]++;
    }
    unsigned int max_freq = 0;
    unsigned int prediction_result;
    for (j = 0; j < NUM_TRAIN_CASES; j++) {
      if (freq_map[j] > max_freq) {
        max_freq = freq_map[j];
        prediction_result = j;
      }
    }
	printf("result = %d for i = %d\n", prediction_result, i); 
  }
   free(freq_map);
}
