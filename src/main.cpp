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

static FrequencyBands current_bands = {0, 0, 0, 0, 0};
static BeatState current_beat = {false, 0, 0};

static uint16_t audio_log_counter = 0;

// Button state
static unsigned long btn_mode_pressed_at = 0;
static bool btn_mode_was_pressed = false;

static void handle_buttons() {
    bool pressed = digitalRead(BTN_MODE_PIN) == LOW;

    if (pressed && !btn_mode_was_pressed) {
        btn_mode_pressed_at = millis();
        btn_mode_was_pressed = true;
        Serial.println("[BTN] MODE pressed");
    }

    if (!pressed && btn_mode_was_pressed) {
        unsigned long duration = millis() - btn_mode_pressed_at;
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

    // Brightness button
    if (digitalRead(BTN_BRIGHT_PIN) == LOW) {
        uint8_t b = led_get_brightness();
        b = (b >= 200) ? 30 : b + 40;
        led_set_brightness(b);
        delay(200);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Music LED Strip - Initializing...");

    pinMode(BTN_MODE_PIN, INPUT_PULLUP);
    pinMode(BTN_BRIGHT_PIN, INPUT_PULLUP);

    led_controller_init();
    audio_capture_init();
    fft_processor_init();
    beat_detector_init();
    mode_manager_init();

    Serial.println("Ready! Mode: Audio Reactive");
    Serial.printf("Pattern: %s\n", mode_manager_get_pattern_name());

    fill_solid(leds, NUM_LEDS, CRGB::Red);
    led_show();
    delay(1000);
}

void loop() {
    unsigned long now = millis();

    handle_buttons();

    // Audio processing tick (~43Hz)
    if (now - last_audio_tick >= AUDIO_TICK_MS) {
        last_audio_tick = now;

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
                Serial.printf("[BANDS] bass=%.3f lo_mid=%.3f mid=%.3f high=%.3f\n",
                    current_bands.bass, current_bands.low_mid,
                    current_bands.mid, current_bands.high);
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
        }

        led_show();
    }
}
