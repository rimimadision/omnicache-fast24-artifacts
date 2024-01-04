#include <cassert>
#include <fcntl.h>
#include <iostream>
#include <random>
#include <string.h>
#include <omp.h>
#include <unistd.h>
#include <vector>
#include <cstring>  // Added include directive
#include <cstdlib>  // Added include directive

#include "data_generator.h"
#include "crfslib.h"
#include "knn_const.h"

using namespace std;

#if 0
int random(int upper, unsigned int *tid) { return rand_r(tid) % upper; }

string supply_leadings(string orig, int len) {
  assert(orig.size() <= len);
  while (orig.size() < len) {
    orig = "0" + orig;
  }
  return orig;
}

string supply_leadings(string orig, int len) {
    return string(len - orig.size(), '0') + orig;
}

string random_result(unsigned int *tid) {
  string ret = to_string(random(NUM_RESULTS, tid));
  return supply_leadings(ret, RESULT_SIZE);
}

string random_weight(unsigned int *tid) {
  string ret = to_string(random(MAX_FEATURE_WEIGHT, tid));
  return supply_leadings(ret, WEIGHT_SIZE);
}
#endif

int random(int upper) {
    static thread_local random_device rd;
    static thread_local mt19937 rng(rd());
    return rng() % upper;
}

string supply_leadings(string orig, int len) {
    return string(len - orig.size(), '0') + orig;
}

string random_result() {
    return supply_leadings(to_string(random(NUM_RESULTS)), RESULT_SIZE);
}

string random_weight() {
    return supply_leadings(to_string(random(MAX_FEATURE_WEIGHT)), WEIGHT_SIZE);
}



void gen_file(int fd) {

 //int fd = crfsopen("/mnt/ram/knn_data.txt", O_RDWR | O_CREAT, 666);
  //ios::sync_with_stdio(false);

    off_t offset = 0;
    if (fd < 0) {
        printf("open error\n");
    }

#if 1
    int num_train_cases = NUM_TRAIN_CASES;
    // const char* output_file_path = argv[2];


    const int BUFFER_SIZE = RESULT_SIZE + FEATURE_DIM * WEIGHT_SIZE;
    const int BATCH_SIZE = 1000;  // Adjust the batch size as needed

    vector<char> buffer(BUFFER_SIZE * BATCH_SIZE);

    int round = 1;
    // Write the data to the file in batches
    for (int i = 0; i < num_train_cases; i += BATCH_SIZE) {
        int batch_count = min(BATCH_SIZE, num_train_cases - i);

        // Fill the buffer with random results and feature vectors
        for (int j = 0; j < batch_count; j++) {
            string result = random_result();
            string feature_vec;

            // Construct the feature vector in the buffer
            char* feature_ptr = &buffer[j * BUFFER_SIZE + RESULT_SIZE];
            for (int k = 0; k < FEATURE_DIM; k++) {
                string weight = random_weight();
                memcpy(feature_ptr, weight.data(), WEIGHT_SIZE);
                feature_ptr += WEIGHT_SIZE;
            }

            // Copy the result to the buffer
            memcpy(&buffer[j * BUFFER_SIZE], result.data(), RESULT_SIZE);
        }

        for (int r = 0; r < round; r++) {
            crfswrite(fd, buffer.data(), BUFFER_SIZE * batch_count);
        }
    }
    // fclose(fd);
#else
#if 0
//      omp_set_num_threads(DATA_GEN_NUM_THREADS);
//#pragma omp parallel
  {
    unsigned int tid = 0x456;
    for (int i = 0; i < NUM_TRAIN_CASES / DATA_GEN_NUM_THREADS; i++) {
      string result = random_result(&tid);
      vector<string> feature_vec;
      for (int j = 0; j < FEATURE_DIM; j++) {
        feature_vec.push_back(random_weight(&tid));
      }
//#pragma omp critical
      {
        //cout << result;
        crfswrite(fd, result.c_str(), strlen(result.c_str()));
        for (auto &str : feature_vec) {
            crfswrite(fd, str.c_str(), strlen(str.c_str()));
            //cout << str;
        }
      }
    }
  }
#else
  constexpr size_t buffer_size = 4096; // 4KB buffer
char buffer[buffer_size];
size_t buffer_index = 0;

auto buffer_write = [&](const std::string& str) {
    const char* data = str.c_str();
    size_t data_size = str.size();
    while (data_size > 0) {
        size_t space_left = buffer_size - buffer_index;
        size_t to_write = std::min(space_left, data_size);

        memcpy(buffer + buffer_index, data, to_write);
        buffer_index += to_write;

        if (buffer_index == buffer_size) {
            crfswrite(fd, buffer, buffer_index);
            buffer_index = 0;
        }

        data += to_write;
        data_size -= to_write;
    }
};

    unsigned int tid =0x156;
    int round = 10;
    for (int r = 0; r < round; r++) {
        for (int i = 0; i < NUM_TRAIN_CASES; i++) {
            string result = random_result(&tid);
            vector<string> feature_vec;
            for (int j = 0; j < FEATURE_DIM; j++) {
                feature_vec.push_back(random_weight(&tid));
            }

            buffer_write(result);
            for (const auto& str : feature_vec) {
                buffer_write(str);
            }
        }
    }

// Write any remaining data in the buffer
if (buffer_index > 0) {
    crfswrite(fd, buffer, buffer_index);
}

#endif
#if 0
    //unsigned int tid = 0x18;
    for (int k = 0; k<DATA_GEN_NUM_THREADS;k++) {
        //unsigned int tid = omp_get_thread_num();
        unsigned int tid = 0x18 * k + 0x5;
        for (int i = 0; i < NUM_TRAIN_CASES / DATA_GEN_NUM_THREADS; i++) {
            string result = random_result(&tid);
            vector<string> feature_vec;
            for (int j = 0; j < FEATURE_DIM; j++) {
                feature_vec.push_back(random_weight(&tid));
            }
            //cout << result;
            crfspwrite(fd, result.c_str(), result.size(), offset);
            offset += result.size();
            for (auto &str : feature_vec) {
                crfspwrite(fd, str.c_str(), str.size(), offset);
                offset += result.size();
                //cout << str;
            }
        }
    }
#endif
#endif
}
