#include <Arduino.h>
#include "config.h"
#include "audio/audio_capture.h"
#include "audio/fft_processor.h"
#include "audio/beat_detector.h"
#include "led/led_controller.h"
#include "led/patterns.h"
#include "modes/mode_manager.h"

static int32_t audio_buffer[SAMPLES];
static unsigned long last_audio_tick = 0;
static unsigned long last_led_render = 0;

static FrequencyBands current_bands = {};
static BeatState current_beat = {false, 0, 0};

static uint16_t audio_log_counter = 0;

// Button state
static unsigned long btn_mode_pressed_at = 0;
static bool btn_mode_was_pressed = false;
static bool btn_bright_was_pressed = false;
static unsigned long btn_bright_changed_at = 0;

// `now` is passed in so every button decision in a single loop() iteration uses
// one consistent timestamp.
static void handle_buttons(unsigned long now) {
    bool pressed = digitalRead(BTN_MODE_PIN) == LOW;

    if (pressed && !btn_mode_was_pressed) {
        btn_mode_pressed_at = now;
        btn_mode_was_pressed = true;
        Serial.println("[BTN] MODE pressed");
    }

    if (!pressed && btn_mode_was_pressed) {
        unsigned long duration = now - btn_mode_pressed_at;
        Serial.printf("[BTN] MODE released, duration=%lums\n", duration);
        if (duration >= BTN_LONG_PRESS_MS) {
            Mode m = mode_manager_get_mode();
            if (m == Mode::AUDIO_REACTIVE) {
                mode_manager_set_mode(Mode::MANUAL);
                Serial.println("[BTN] -> MANUAL mode");
            } else {
                mode_manager_set_mode(Mode::AUDIO_REACTIVE);
                Serial.println("[BTN] -> AUDIO_REACTIVE mode");
            }
        } else if (duration >= BTN_DEBOUNCE_MS) {
            mode_manager_next_pattern();
            Serial.printf("[BTN] -> pattern %d: %s\n", mode_manager_get_pattern_index(), mode_manager_get_pattern_name());
        } else {
            Serial.println("[BTN] ignored (too short)");
        }
        btn_mode_was_pressed = false;
    }

    // Brightness button: one step per press, taken on the falling edge.
    // The previous version changed brightness on every loop() iteration while
    // the button was held and used delay(200) to slow that down. That delay
    // blocked loop(), dropping roughly 9 audio frames and 12 LED frames per
    // press, and holding the button ramped brightness continuously.
    bool bright_pressed = digitalRead(BTN_BRIGHT_PIN) == LOW;
    if (bright_pressed != btn_bright_was_pressed &&
        (now - btn_bright_changed_at) >= BTN_DEBOUNCE_MS) {
        btn_bright_changed_at = now;
        btn_bright_was_pressed = bright_pressed;
        if (bright_pressed) {
            uint8_t b = led_get_brightness();
            b = (b >= 200) ? 30 : b + 40;
            led_set_brightness(b);
            Serial.printf("[BTN] brightness=%u\n", b);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Music LED Strip - Initializing...");

    pinMode(BTN_MODE_PIN, INPUT_PULLUP);
    pinMode(BTN_BRIGHT_PIN, INPUT_PULLUP);

    led_controller_init();

    // Startup flash BEFORE audio_capture_init(). Starting I2S and then sitting
    // in delay(1000) leaves the DMA ring un-drained for ~20 ring depths, which
    // guarantees a backlog of dropped buffers before loop() ever runs.
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    led_show();
    delay(1000);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    led_show();

    audio_capture_init();
    fft_processor_init();
    beat_detector_init();
    mode_manager_init();

    Serial.println("Ready! Mode: Audio Reactive");
    Serial.printf("Pattern: %s\n", mode_manager_get_pattern_name());

    last_audio_tick = millis();
}

void loop() {
    unsigned long now = millis();

    handle_buttons(now);

    // Audio processing tick (~43Hz)
    // Advance by exactly AUDIO_TICK_MS rather than resetting to now, so the
    // schedule does not drift: `= now` makes the real period AUDIO_TICK_MS plus
    // however long the rest of the loop took, and any period above 23.22 ms is a
    // permanent deficit against the microphone that no ring depth can absorb.
    // 23 ms is marginally faster than production, so i2s_read blocks briefly and
    // the audio path ends up paced by the I2S clock itself.
    if (now - last_audio_tick >= AUDIO_TICK_MS) {
        last_audio_tick += AUDIO_TICK_MS;
        // If we fell far behind (a long blocking operation), resync instead of
        // running a catch-up burst.
        if (now - last_audio_tick >= AUDIO_TICK_MS) last_audio_tick = now;

        size_t samples_read = audio_capture_read(audio_buffer, SAMPLES);
        if (samples_read != SAMPLES) {
            Serial.printf("[MIC] read %u/%u samples\n", samples_read, SAMPLES);
        } else {
            current_bands = fft_process(audio_buffer, SAMPLES);
            current_beat = beat_detect(current_bands);

            if (current_beat.beat_detected) {
                Serial.printf("[BEAT] intensity=%.2f decay=%.2f\n",
                    current_beat.intensity, current_beat.decay);
            }

            if (++audio_log_counter >= 43) {
                audio_log_counter = 0;
                int32_t max_raw = 0;
                for (size_t i = 0; i < SAMPLES; i++) {
                    int32_t v = audio_buffer[i] < 0 ? -audio_buffer[i] : audio_buffer[i];
                    if (v > max_raw) max_raw = v;
                }
                Serial.printf("[MIC] max_raw=%ld\n", max_raw);
                // Magnitudes are gain-normalized, so they are small; print
                // enough decimals to be useful when tuning.
                Serial.printf("[BANDS] bass=%.5f lo_mid=%.5f mid=%.5f high=%.5f\n",
                    current_bands.bass, current_bands.low_mid,
                    current_bands.mid, current_bands.high);
                Serial.printf("[NORM]  bass=%.2f lo_mid=%.2f mid=%.2f high=%.2f\n",
                    current_bands.norm[BAND_BASS], current_bands.norm[BAND_LOW_MID],
                    current_bands.norm[BAND_MID], current_bands.norm[BAND_HIGH]);
            }
        }
    }

    // LED render tick (~60 FPS)
    if (now - last_led_render >= LED_RENDER_MS) {
        last_led_render = now;

        if (mode_manager_get_mode() == Mode::AUDIO_REACTIVE) {
            Pattern* pattern = mode_manager_get_current_pattern();
            pattern->update(current_bands, current_beat);
            pattern->render(leds, NUM_LEDS);
            // Rendering runs at 16ms but audio only refreshes every 23ms, so a
            // single beat would otherwise be seen by update() on two
            // consecutive frames (double hue jumps, skipped SKZ colors).
            // Consume the edge; decay and band levels stay live.
            current_beat.beat_detected = false;
        }

        led_show();
    }
}
