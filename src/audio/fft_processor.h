#pragma once

#include <cstdint>

struct FrequencyBands {
    float bass;
    float low_mid;
    float mid;
    float high;
    float overall;
};

void fft_processor_init();
FrequencyBands fft_process(const int32_t* samples, size_t num_samples);
