#include <Arduino.h>
#include "config.h"
#include "audio/audio_capture.h"
#include "audio/fft_processor.h"
#include "audio/beat_detector.h"
#include "led/led_controller.h"
#include "led/patterns.h"
#include "modes/mode_manager.h"
#include "settings/settings.h"
#include "web/web_server.h"

static int32_t audio_buffer[SAMPLES];
static unsigned long last_audio_tick = 0;
static unsigned long last_led_render = 0;

static FrequencyBands current_bands = {};
static BeatState current_beat = {false, 0, 0};

static uint16_t audio_log_counter = 0;

// Diagnostic accumulators. Every one of these covers EVERY audio tick in the
// reporting window. Printing the current window instead observed only 1024
// samples out of each ~43000, a 2.3% duty cycle, so a transient such as a hand
// clap was missed roughly 97% of the time. That makes a responsive microphone
// indistinguishable from a dead one and invalidates any dynamic-range estimate
// read off these lines.
static int32_t log_peak_raw = 0;
static float log_band_min[BAND_COUNT];
static float log_band_max[BAND_COUNT];
static uint16_t log_beats = 0;
static float log_peak_mag = 0.0f;      // strongest bin seen in the window
static uint16_t log_peak_bin = 0;      // and which bin it was

static void diag_window_reset() {
    log_peak_raw = 0;
    log_beats = 0;
    log_peak_mag = 0.0f;
    log_peak_bin = 0;
    for (int i = 0; i < BAND_COUNT; i++) {
        log_band_min[i] = 1e9f;
        log_band_max[i] = 0.0f;
    }
}

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

    // mode_manager_init() resets mode and pattern to defaults, so it MUST come
    // before settings_init(), which then applies whatever was restored from NVS
    // over the top of those defaults.
    mode_manager_init();
    settings_init();

    // Startup flash BEFORE audio_capture_init(). Starting I2S and then sitting
    // in delay(1000) leaves the DMA ring un-drained for ~20 ring depths, which
    // guarantees a backlog of dropped buffers before loop() ever runs.
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    led_show();
    delay(1000);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    led_show();

    // Bringing up the AP takes long enough to matter, so it happens before I2S
    // for the same reason the startup flash does.
    web_server_init();

    fft_processor_init();
    beat_detector_init();

    // I2S LAST, so the DMA ring starts filling as late as possible and loop()
    // begins draining it immediately.
    audio_capture_init();

    Serial.printf("Ready! Mode: %u  Pattern: %s\n",
        (unsigned)mode_manager_get_mode(), mode_manager_get_pattern_name());

    diag_window_reset();
    last_audio_tick = millis();
}

void loop() {
    unsigned long now = millis();

    handle_buttons(now);

    // Apply anything the core-0 web task queued, then republish the snapshot it
    // reads. Done here, before the render tick, so a change made from the phone
    // is visible on this same iteration. Costs one non-blocking queue peek when
    // there is nothing pending.
    web_server_service(now);

    // Debounced write-back of mode/pattern/brightness to NVS. Detects changes by
    // comparison, so it covers the web path, the button path and any future one.
    settings_service(now);

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
                log_beats++;
            }

            int32_t max_raw = 0;
            for (size_t i = 0; i < SAMPLES; i++) {
                int32_t v = audio_buffer[i] < 0 ? -audio_buffer[i] : audio_buffer[i];
                if (v > max_raw) max_raw = v;
            }
            if (max_raw > log_peak_raw) log_peak_raw = max_raw;

            const float band_now[BAND_COUNT] = {
                current_bands.bass, current_bands.low_mid,
                current_bands.mid, current_bands.high
            };
            for (int i = 0; i < BAND_COUNT; i++) {
                if (band_now[i] < log_band_min[i]) log_band_min[i] = band_now[i];
                if (band_now[i] > log_band_max[i]) log_band_max[i] = band_now[i];
            }

            if (current_bands.peak_mag > log_peak_mag) {
                log_peak_mag = current_bands.peak_mag;
                log_peak_bin = current_bands.peak_bin;
            }

            if (++audio_log_counter >= 43) {
                audio_log_counter = 0;
                // Peak over the whole window, so a transient cannot slip between
                // two reports.
                Serial.printf("[MIC] peak_raw=%ld (%.3f%%FS)\n",
                    (long)log_peak_raw, 100.0f * (float)log_peak_raw / 2147483392.0f);
                // Min..max per band over the whole window. A band that is only
                // noise has a narrow range; real content widens it.
                Serial.printf("[BANDS] bass=%.5f..%.5f lo_mid=%.5f..%.5f "
                              "mid=%.5f..%.5f high=%.5f..%.5f\n",
                    log_band_min[BAND_BASS], log_band_max[BAND_BASS],
                    log_band_min[BAND_LOW_MID], log_band_max[BAND_LOW_MID],
                    log_band_min[BAND_MID], log_band_max[BAND_MID],
                    log_band_min[BAND_HIGH], log_band_max[BAND_HIGH]);
                Serial.printf("[NORM]  bass=%.2f lo_mid=%.2f mid=%.2f high=%.2f "
                              "beats=%u/43\n",
                    current_bands.norm[BAND_BASS], current_bands.norm[BAND_LOW_MID],
                    current_bands.norm[BAND_MID], current_bands.norm[BAND_HIGH],
                    log_beats);
                // Strongest single bin in the window. Broadband noise leaves no
                // bin standing out; any tone, note or voice produces one well
                // above the band means printed above.
                Serial.printf("[TONE]  bin=%u f=%.0fHz mag=%.5f (%.0fx the mid band mean)\n",
                    log_peak_bin,
                    (float)log_peak_bin * (float)SAMPLE_RATE / (float)SAMPLES,
                    log_peak_mag,
                    log_band_max[BAND_MID] > 0.0f ? log_peak_mag / log_band_max[BAND_MID] : 0.0f);
                // Which gate is limiting. A beat needs all four to pass, so
                // the smallest percentage is the binding constraint.
                // crestkill is the share of frames where the noise floor and
                // the flux threshold both passed and only the crest test
                // rejected. If that stays high while music plays, and only
                // then, BEAT_CREST_FACTOR is what is eating the beats. Check
                // the microphone with `pio run -e probe` first.
                BeatGateStats g = beat_detector_stats_take();
                float pct = g.frames ? 100.0f / (float)g.frames : 0.0f;
                Serial.printf("[GATE]  floor=%.0f%% crest=%.0f%% flux=%.0f%% "
                              "crestkill=%.0f%% (of %u frames)\n",
                    g.pass_floor * pct, g.pass_crest * pct,
                    g.pass_flux * pct, g.rej_crest_with_flux * pct, g.frames);
                diag_window_reset();
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
