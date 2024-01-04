#include "const.h"
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include <cstring>  // Added include directive
#include <cstdlib>  // Added include directive

using namespace std;

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

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <num_train_cases> <output_file_path>\n", argv[0]);
        return 1;
    }

    int num_train_cases = atoi(argv[1]);
    const char* output_file_path = argv[2];

    FILE* fd = fopen(output_file_path, "wb");
    if (!fd) {
        printf("Failed to open the file.\n");
        return 1;
    }

    const int BUFFER_SIZE = RESULT_SIZE + FEATURE_DIM * WEIGHT_SIZE;
    const int BATCH_SIZE = 1000;  // Adjust the batch size as needed

    vector<char> buffer(BUFFER_SIZE * BATCH_SIZE);

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

        // Write the batch to the file
        fwrite(buffer.data(), 1, BUFFER_SIZE * batch_count, fd);
    }

    fclose(fd);
    return 0;
}

