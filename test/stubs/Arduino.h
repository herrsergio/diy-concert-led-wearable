#pragma once

// Minimal Arduino.h replacement for the `native` test environment, so the
// audio modules can be compiled and exercised on the host. Only the pieces
// beat_detector.cpp actually uses are provided.

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <algorithm>

// Tests drive time explicitly instead of waiting in real time, so each audio
// frame lands exactly AUDIO_TICK_MS apart and beat counts are deterministic.
extern unsigned long test_millis_value;

inline unsigned long millis() { return test_millis_value; }

using std::min;
using std::max;
