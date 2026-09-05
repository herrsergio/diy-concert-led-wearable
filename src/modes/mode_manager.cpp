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
    &rainbow_wave,
    &pulse_beat,
    &strobe_kick,
    &freq_bars,
    &skz_colors
};

// Kept at file scope, not inside mode_manager_get_pattern_name(), so a UI can
// enumerate every name without cycling the active pattern.
//
// The order MUST match patterns[] above, and note that it is NOT the declaration
// order in patterns.h, where PulseBeat is declared first. The two orders really
// do differ, so adding a pattern means editing both arrays in the same order.
static const char* const PATTERN_NAMES[NUM_PATTERNS] = {
    "RainbowWave",
    "PulseBeat",
    "StrobeKick",
    "FrequencyBars",
    "SKZColors"
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
    return PATTERN_NAMES[pattern_idx];
}

void mode_manager_set_pattern(uint8_t index) {
    if (index < NUM_PATTERNS) pattern_idx = index;
}

const char* mode_manager_get_pattern_name_at(uint8_t index) {
    return (index < NUM_PATTERNS) ? PATTERN_NAMES[index] : nullptr;
}

uint8_t mode_manager_pattern_count() {
    return NUM_PATTERNS;
}

bool mode_manager_is_valid_mode(uint8_t value) {
    return value == (uint8_t)Mode::AUDIO_REACTIVE ||
           value == (uint8_t)Mode::BLE_SYNC ||
           value == (uint8_t)Mode::MANUAL;
}
