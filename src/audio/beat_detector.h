#pragma once

#include "fft_processor.h"

struct BeatState {
    bool beat_detected;
    float intensity;     // 0.0 - 1.0, how strong the beat is
    float decay;         // Exponential decay value for smooth animations
};

// Which gate rejected each frame. Counted independently rather than as a
// partition, because the question that matters is not "how many frames failed"
// but "which gate is the binding constraint". A hand clap passes all four,
// while dense music can produce a genuine onset (flux over threshold) that the
// crest test then discards, because bass_level_avg tracks the music's own bass
// level. rej_crest_with_flux is that case, and it is the number to watch.
struct BeatGateStats {
    uint16_t frames;
    uint16_t pass_floor;
    uint16_t pass_crest;
    uint16_t pass_flux;
    uint16_t rej_crest_with_flux;   // floor and flux passed, crest rejected
    uint16_t beats;
};

void beat_detector_init();
BeatState beat_detect(const FrequencyBands& bands);

// Returns the counters accumulated since the last call, and resets them.
BeatGateStats beat_detector_stats_take();
