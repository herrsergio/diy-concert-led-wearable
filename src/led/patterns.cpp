#include "patterns.h"
#include "color_palettes.h"
#include "../config.h"

// --- PulseBeat ---

void PulseBeat::update(const FrequencyBands& bands, const BeatState& beat) {
    if (beat.beat_detected) {
        hue += 32;
    }
    brightness = beat.decay;
}

void PulseBeat::render(CRGB* leds, int num_leds) {
    uint8_t val = (uint8_t)(brightness * 255);
    CRGB color = CHSV(hue, 240, val);
    fill_solid(leds, num_leds, color);
}

// --- RainbowWave ---

void RainbowWave::update(const FrequencyBands& bands, const BeatState& beat) {
    speed = 1.0f + bands.mid * 20.0f;
    if (beat.beat_detected) {
        speed += 5.0f;
    }
    start_hue += (uint8_t)speed;
}

void RainbowWave::render(CRGB* leds, int num_leds) {
    fill_rainbow(leds, num_leds, start_hue, 256 / num_leds);
}

// --- StrobeKick ---

void StrobeKick::update(const FrequencyBands& bands, const BeatState& beat) {
    if (beat.beat_detected && beat.intensity > 0.6f) {
        flash_active = true;
        flash_decay = 1.0f;
    }
    flash_decay *= 0.85f;
    if (flash_decay < 0.05f) {
        flash_active = false;
        flash_decay = 0;
    }
    glow_hue += 1;
}

void StrobeKick::render(CRGB* leds, int num_leds) {
    if (flash_active) {
        uint8_t val = (uint8_t)(flash_decay * 255);
        fill_solid(leds, num_leds, CRGB(val, val, val));
    } else {
        fill_solid(leds, num_leds, CHSV(glow_hue, 200, 60));
    }
}

// --- FrequencyBars ---

void FrequencyBars::update(const FrequencyBands& bands, const BeatState& beat) {
    // Smooth with exponential moving average
    band_levels[0] = band_levels[0] * 0.7f + bands.bass * 0.3f;
    band_levels[1] = band_levels[1] * 0.7f + bands.low_mid * 0.3f;
    band_levels[2] = band_levels[2] * 0.7f + bands.mid * 0.3f;
    band_levels[3] = band_levels[3] * 0.7f + bands.high * 0.3f;
}

void FrequencyBars::render(CRGB* leds, int num_leds) {
    int section = num_leds / 4;
    CRGB colors[4] = {
        CRGB::Red,         // Bass
        CRGB::Orange,      // Low-mid
        CRGB::Green,       // Mid
        CRGB::Blue         // High
    };

    for (int band = 0; band < 4; band++) {
        int start = band * section;
        int end = (band == 3) ? num_leds : start + section;
        int band_leds = end - start;
        int lit_count = (int)(band_levels[band] * band_leds * 40.0f);
        if (lit_count > band_leds) lit_count = band_leds;

        for (int i = 0; i < band_leds; i++) {
            leds[start + i] = (i < lit_count) ? colors[band] : CRGB::Black;
        }
    }
}

// --- SKZColors ---

static const CRGB skz_colors[] = {
    CRGB(57, 255, 20),   // Neon green
    CRGB(255, 0, 0),     // Red
    CRGB(0, 0, 0),       // Black (off/dim white)
    CRGB(255, 255, 255), // White
};
static const uint8_t skz_color_count = sizeof(skz_colors) / sizeof(skz_colors[0]);

void SKZColors::update(const FrequencyBands& bands, const BeatState& beat) {
    if (beat.beat_detected && beat.intensity > 0.5f) {
        color_idx = (color_idx + 1) % skz_color_count;
        target_color = skz_colors[color_idx];
        transition = 0;
    }
    transition += 0.08f;
    if (transition > 1.0f) transition = 1.0f;

    current_color = blend(current_color, target_color, (uint8_t)(transition * 255));
}

void SKZColors::render(CRGB* leds, int num_leds) {
    fill_solid(leds, num_leds, current_color);
}
