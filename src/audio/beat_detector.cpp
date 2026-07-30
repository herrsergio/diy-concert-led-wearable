#include "beat_detector.h"
#include "../config.h"
#include <Arduino.h>

static float bass_history[BEAT_HISTORY_SIZE];
static uint8_t history_idx = 0;
static float prev_bass = 0;
static unsigned long last_beat_time = 0;
static float current_decay = 0;

void beat_detector_init() {
    for (int i = 0; i < BEAT_HISTORY_SIZE; i++) {
        bass_history[i] = 0;
    }
    history_idx = 0;
    prev_bass = 0;
    last_beat_time = 0;
    current_decay = 0;
}

BeatState beat_detect(const FrequencyBands& bands) {
    unsigned long now = millis();
    BeatState state = { false, 0, 0 };

    // Spectral flux: positive difference in bass energy
    float flux = bands.bass - prev_bass;
    if (flux < 0) flux = 0;
    prev_bass = bands.bass;

    // Store in circular buffer
    bass_history[history_idx] = flux;
    history_idx = (history_idx + 1) % BEAT_HISTORY_SIZE;

    // Compute adaptive threshold (mean of history * sensitivity)
    float sum = 0;
    for (int i = 0; i < BEAT_HISTORY_SIZE; i++) {
        sum += bass_history[i];
    }
    float threshold = (sum / BEAT_HISTORY_SIZE) * BEAT_SENSITIVITY;

    // Minimum threshold to avoid triggering on silence
    if (threshold < 0.01f) threshold = 0.01f;

    // Check for beat
    bool min_interval_passed = (now - last_beat_time) >= BEAT_MIN_INTERVAL_MS;
    if (flux > threshold && min_interval_passed) {
        state.beat_detected = true;
        state.intensity = min(flux / (threshold * 2.0f), 1.0f);
        last_beat_time = now;
        current_decay = 1.0f;
    }

    // Exponential decay for smooth pulse animations
    current_decay *= 0.92f;
    if (current_decay < 0.01f) current_decay = 0;
    state.decay = current_decay;

    return state;
}
