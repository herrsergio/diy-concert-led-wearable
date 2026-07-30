#include "mode_manager.h"
#include "../led/patterns.h"
#include "../config.h"

static Mode current_mode = Mode::AUDIO_REACTIVE;
static uint8_t pattern_idx = 0;

static PulseBeat pulse_beat;
static RainbowWave rainbow_wave;
static StrobeKick strobe_kick;
static FrequencyBars freq_bars;
static SKZColors skz_colors;

static Pattern* patterns[NUM_PATTERNS] = {
    &pulse_beat,
    &rainbow_wave,
    &strobe_kick,
    &freq_bars,
    &skz_colors
};

Pattern* mode_manager_get_current_pattern() {
    return patterns[pattern_idx];
}

void mode_manager_init() {
    current_mode = Mode::AUDIO_REACTIVE;
    pattern_idx = 0;
}

void mode_manager_next_pattern() {
    pattern_idx = (pattern_idx + 1) % NUM_PATTERNS;
}

Mode mode_manager_get_mode() {
    return current_mode;
}

void mode_manager_set_mode(Mode mode) {
    current_mode = mode;
}

uint8_t mode_manager_get_pattern_index() {
    return pattern_idx;
}

const char* mode_manager_get_pattern_name() {
    return patterns[pattern_idx]->name();
}
