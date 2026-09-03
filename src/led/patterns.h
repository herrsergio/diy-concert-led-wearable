#pragma once

#include <FastLED.h>
#include "../audio/fft_processor.h"
#include "../audio/beat_detector.h"

#define NUM_PATTERNS 5

class Pattern {
public:
    virtual void update(const FrequencyBands& bands, const BeatState& beat) = 0;
    virtual void render(CRGB* leds, int num_leds) = 0;
    virtual ~Pattern() = default;
};

class PulseBeat : public Pattern {
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
private:
    uint8_t hue = 0;
    float brightness = 0;
};

class RainbowWave : public Pattern {
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
private:
    uint8_t start_hue = 0;
    float speed = 1.0f;
};

class StrobeKick : public Pattern {
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
private:
    bool flash_active = false;
    float flash_decay = 0;
    uint8_t glow_hue = 0;
};

class FrequencyBars : public Pattern {
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
private:
    float band_levels[BAND_COUNT] = {0, 0, 0, 0};
};

class SKZColors : public Pattern {
public:
    void update(const FrequencyBands& bands, const BeatState& beat) override;
    void render(CRGB* leds, int num_leds) override;
private:
    uint8_t color_idx = 0;
    CRGB current_color = CRGB::Black;
    CRGB target_color = CRGB(57, 255, 20);
    float transition = 0;
};
