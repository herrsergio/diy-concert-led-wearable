#pragma once

#include <cstdint>
#include <cstddef>

enum Band {
    BAND_BASS = 0,
    BAND_LOW_MID,
    BAND_MID,
    BAND_HIGH,
    BAND_COUNT
};

struct FrequencyBands {
    // Gain-normalized FFT magnitude: a full-scale sine in the band reads ~1.0.
    // Typical music values are small (1e-4 .. 1e-1), so these are for analysis
    // (beat detection), not for driving LED brightness directly.
    float bass;
    float low_mid;
    float mid;
    float high;
    float overall;

    // Auto-gained 0.0-1.0 per band, indexed by Band. Volume-independent and
    // forced to 0 when the band carries no dynamics. Use these for visuals.
    float norm[BAND_COUNT];

    // Strongest single bin in the spectrum, for deciding whether the input
    // carries tonal content at all. The band values above are per-bin MEANS, so
    // a wide band averages real content down into the noise: `high` spans 279
    // bins against `bass`'s 4. That makes a flat band ambiguous. A single
    // dominant bin is not: broadband noise has no bin standing out from its
    // neighbours, while a note, a voice or a whistle produces one.
    // peak_mag is gain-normalized on the same scale as the band values.
    uint16_t peak_bin;
    float peak_mag;
};

void fft_processor_init();
FrequencyBands fft_process(const int32_t* samples, size_t num_samples);
