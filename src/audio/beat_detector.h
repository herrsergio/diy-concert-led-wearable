#pragma once

#include "fft_processor.h"

struct BeatState {
    bool beat_detected;
    float intensity;     // 0.0 - 1.0, how strong the beat is
    float decay;         // Exponential decay value for smooth animations
};

void beat_detector_init();
BeatState beat_detect(const FrequencyBands& bands);
