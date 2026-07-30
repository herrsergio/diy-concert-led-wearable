#pragma once

#include <FastLED.h>
#include "../audio/fft_processor.h"
#include "../audio/beat_detector.h"

class Pattern {
public:
    virtual ~Pattern() = default;
    virtual void update(const FrequencyBands& bands, const BeatState& beat) = 0;
    virtual void render(CRGB* leds, int num_leds) = 0;
    virtual const char* name() = 0;
};

// Full strip pulses on beat, color from palette
class PulseBeat : public Pattern {
    uint8_t hue = 0;
    float brightness = 0;
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
    const char* name() override { return "PulseBeat"; }
};

// Rainbow hue shifts with energy, speed tied to intensity
class RainbowWave : public Pattern {
    uint8_t start_hue = 0;
    float speed = 1.0f;
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
    const char* name() override { return "RainbowWave"; }
};

// White flash on strong beat, colored glow between
class StrobeKick : public Pattern {
    bool flash_active = false;
    uint8_t glow_hue = 0;
    float flash_decay = 0;
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
    const char* name() override { return "StrobeKick"; }
};

// Map frequency bands to strip segments
class FrequencyBars : public Pattern {
    float band_levels[4] = {0, 0, 0, 0};
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
    const char* name() override { return "FreqBars"; }
};

// Cycles through SKZ colors synced to beat
class SKZColors : public Pattern {
    uint8_t color_idx = 0;
    float transition = 0;
    CRGB current_color = CRGB::Black;
    CRGB target_color = CRGB(57, 255, 20); // Neon green
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
    const char* name() override { return "SKZColors"; }
};

#define NUM_PATTERNS 5
