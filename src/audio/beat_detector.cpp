#include "beat_detector.h"
#include "../config.h"
#include <Arduino.h>
#include <math.h>

// Spectral-flux onset detection.
//
// The threshold is mean + k*stddev of the recent flux, not mean * k. That
// distinction is the whole fix: for any stationary signal the typical positive
// flux is close to the mean of the rectified flux, so a plain multiplier below
// ~2.0 is crossed on roughly half of all frames and the minimum-interval limiter
// turns that into a steady false-beat metronome. Measured on white noise at
// -40 dBFS with no music at all: mean*1.5 fired 3.95 beats/s out of a 5.00/s
// ceiling. mean + 3*stddev fires 0.07-0.22/s on the same input.
//
// A loudness gate cannot substitute for this, because the bass level of a noisy
// room overlaps the bass level of quiet music. Discrimination has to come from
// the shape of the signal, which is what the stddev and crest tests measure.

static float flux_history[BEAT_HISTORY_SIZE];
static uint8_t history_idx = 0;
static uint16_t frames_seen = 0;
static float prev_bass = 0;
static float bass_level_avg = 0;
static unsigned long last_beat_time = 0;
static float current_decay = 0;

void beat_detector_init() {
    for (int i = 0; i < BEAT_HISTORY_SIZE; i++) {
        flux_history[i] = 0;
    }
    history_idx = 0;
    frames_seen = 0;
    prev_bass = 0;
    bass_level_avg = 0;
    last_beat_time = 0;
    current_decay = 0;
}

BeatState beat_detect(const FrequencyBands& bands) {
    unsigned long now = millis();
    BeatState state = { false, 0, 0 };

    // Half-wave rectified spectral flux in the bass band
    float flux = bands.bass - prev_bass;
    if (flux < 0) flux = 0;
    prev_bass = bands.bass;

    // Slow level reference for the crest test
    bass_level_avg = bass_level_avg * (1.0f - BEAT_LEVEL_AVG_ALPHA) +
                     bands.bass * BEAT_LEVEL_AVG_ALPHA;

    // Mean and standard deviation of the recent flux
    float sum = 0;
    for (int i = 0; i < BEAT_HISTORY_SIZE; i++) {
        sum += flux_history[i];
    }
    float mean = sum / BEAT_HISTORY_SIZE;

    float var_sum = 0;
    for (int i = 0; i < BEAT_HISTORY_SIZE; i++) {
        float d = flux_history[i] - mean;
        var_sum += d * d;
    }
    float stddev = sqrtf(var_sum / BEAT_HISTORY_SIZE);

    float threshold = mean + BEAT_SENSITIVITY_SIGMA * stddev;

    bool warmed_up = frames_seen >= BEAT_HISTORY_SIZE;
    bool above_floor = bands.bass >= BEAT_NOISE_FLOOR;
    bool interval_passed = (now - last_beat_time) >= BEAT_MIN_INTERVAL_MS;
    bool crest_passed = bands.bass > bass_level_avg * BEAT_CREST_FACTOR;

    bool is_beat = warmed_up && above_floor && interval_passed &&
                   crest_passed && flux > threshold;

    if (is_beat) {
        state.beat_detected = true;
        // How far past the threshold, expressed as 0.0-1.0
        float headroom = threshold - mean;
        state.intensity = (headroom > 0) ? (flux - mean) / (headroom * 2.0f) : 1.0f;
        if (state.intensity > 1.0f) state.intensity = 1.0f;
        if (state.intensity < 0.0f) state.intensity = 0.0f;
        last_beat_time = now;
        current_decay = 1.0f;
    }

    // The history must describe the *background*, so beat frames are excluded.
    // Feeding a kick's flux back in inflates stddev enough to mask the next
    // kick; excluding them recovered exact beat tracking (2.00/s at 120 BPM)
    // where including them dropped it to 1.55/s.
    if (!is_beat) {
        flux_history[history_idx] = flux;
        history_idx = (history_idx + 1) % BEAT_HISTORY_SIZE;
    }
    if (frames_seen < BEAT_HISTORY_SIZE) frames_seen++;

    // Exponential decay for smooth pulse animations
    current_decay *= BEAT_DECAY_PER_TICK;
    if (current_decay < 0.01f) current_decay = 0;
    state.decay = current_decay;

    return state;
}
