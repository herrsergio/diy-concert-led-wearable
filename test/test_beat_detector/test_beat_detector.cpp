// Host-side regression tests for the audio pipeline.
//
// Run with:  pio test -e native
//
// These exist because the original bug could not be reproduced by inspection:
// the detector fired ~4 false beats per second on plain room noise while looking
// perfectly reasonable in source form. The tests below drive the real
// beat_detector against synthetic silence, noise and beats so a regression shows
// up as a number instead of as a strip that flickers at a concert.
//
// The FFT stage is replaced by a small local implementation because arduinoFFT
// and the ESP32 I2S driver are not available on the host. The band extraction
// and normalization mirror src/audio/fft_processor.cpp.

#include <unity.h>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <random>
#include <algorithm>

#include "../../src/config.h"
#include "../../src/audio/fft_processor.h"
#include "../../src/audio/beat_detector.h"

// ---------------------------------------------------------------------------
// Minimal FFT mirroring the firmware's Hamming + magnitude pipeline
// ---------------------------------------------------------------------------

static double v_real[SAMPLES];
static double v_imag[SAMPLES];

static void dc_removal() {
    double mean = 0;
    for (int i = 0; i < SAMPLES; i++) mean += v_real[i];
    mean /= SAMPLES;
    for (int i = 0; i < SAMPLES; i++) v_real[i] -= mean;
}

static void windowing_hamming() {
    for (int i = 0; i < SAMPLES / 2; i++) {
        double ratio = (double)i / (double)(SAMPLES - 1);
        double w = 0.54 - 0.46 * cos(2.0 * M_PI * ratio);
        v_real[i] *= w;
        v_real[SAMPLES - 1 - i] *= w;
    }
}

static void compute_magnitude() {
    int j = 0;
    for (int i = 0; i < SAMPLES - 1; i++) {
        if (i < j) { std::swap(v_real[i], v_real[j]); std::swap(v_imag[i], v_imag[j]); }
        int k = SAMPLES >> 1;
        while (k <= j) { j -= k; k >>= 1; }
        j += k;
    }
    for (int len = 2; len <= SAMPLES; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        for (int i = 0; i < SAMPLES; i += len) {
            for (int k = 0; k < len / 2; k++) {
                double wr = cos(ang * k), wi = sin(ang * k);
                double ur = v_real[i + k], ui = v_imag[i + k];
                double vr = v_real[i + k + len / 2], vi = v_imag[i + k + len / 2];
                double tr = vr * wr - vi * wi, ti = vr * wi + vi * wr;
                v_real[i + k] = ur + tr;             v_imag[i + k] = ui + ti;
                v_real[i + k + len / 2] = ur - tr;   v_imag[i + k + len / 2] = ui - ti;
            }
        }
    }
    for (int i = 0; i < SAMPLES; i++)
        v_real[i] = sqrt(v_real[i] * v_real[i] + v_imag[i] * v_imag[i]);
}

static float band_energy(uint16_t freq_low, uint16_t freq_high) {
    const float bin_width = (float)SAMPLE_RATE / SAMPLES;
    int bin_low = (int)(freq_low / bin_width);
    int bin_high = (int)(freq_high / bin_width);
    if (bin_low < 1) bin_low = 1;
    if (bin_high > SAMPLES / 2) bin_high = SAMPLES / 2;
    if (bin_high <= bin_low) bin_high = bin_low + 1;
    float energy = 0;
    for (int i = bin_low; i < bin_high; i++) energy += (float)v_real[i];
    return (energy / (bin_high - bin_low)) / FFT_MAG_SCALE;
}

static FrequencyBands analyze(const int32_t* samples) {
    for (int i = 0; i < SAMPLES; i++) {
        v_real[i] = (double)(samples[i] >> 8) / 8388608.0;
        v_imag[i] = 0.0;
    }
    dc_removal();
    windowing_hamming();
    compute_magnitude();

    FrequencyBands b = {};
    b.bass    = band_energy(FFT_BAND_BASS_LOW, FFT_BAND_BASS_HIGH);
    b.low_mid = band_energy(FFT_BAND_BASS_HIGH, FFT_BAND_LOWMID_HIGH);
    b.mid     = band_energy(FFT_BAND_LOWMID_HIGH, FFT_BAND_MID_HIGH);
    b.high    = band_energy(FFT_BAND_MID_HIGH, FFT_BAND_HIGH_HIGH);
    b.overall = (b.bass + b.low_mid + b.mid + b.high) / 4.0f;
    return b;
}

// ---------------------------------------------------------------------------
// Signal generation, in raw I2S 24-bit-left-aligned-in-32-bit format
// ---------------------------------------------------------------------------

static std::mt19937 rng(20260903);

// bpm == 0 produces noise only. dc adds a constant offset as a fraction of
// full scale, standing in for a microphone with a DC bias.
static void generate(int32_t* buf, double bpm, double amp, double noise,
                     double dc, uint64_t phase) {
    std::normal_distribution<double> n(0.0, noise);
    double period = bpm > 0 ? 60.0 / bpm : 0;
    for (int i = 0; i < SAMPLES; i++) {
        double t = (double)(phase + i) / SAMPLE_RATE;
        double s = n(rng) + dc;
        if (bpm > 0) {
            double bp = fmod(t, period);
            s += amp * exp(-bp * 18.0) * sin(2.0 * M_PI * 80.0 * t);
        }
        s = std::clamp(s, -0.99, 0.99);
        buf[i] = (int32_t)(s * 8388608.0) << 8;
    }
}

// millis() is stubbed for the native build (see test/stubs/Arduino.h) so the
// detector advances in exact AUDIO_TICK_MS steps rather than in real time.
unsigned long test_millis_value = 0;

static int count_beats(double bpm, double amp, double noise, double dc, int ticks) {
    beat_detector_init();
    static int32_t buf[SAMPLES];
    uint64_t phase = 0;
    int beats = 0;
    for (int t = 0; t < ticks; t++) {
        test_millis_value = (unsigned long)t * AUDIO_TICK_MS;
        generate(buf, bpm, amp, noise, dc, phase);
        phase += SAMPLES;
        if (beat_detect(analyze(buf)).beat_detected) beats++;
    }
    return beats;
}

static const int TICKS_20S = 20000 / AUDIO_TICK_MS;
static const double SECONDS_20S = TICKS_20S * AUDIO_TICK_MS / 1000.0;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// The original defect: LEDs pulsed with no music playing. Ambient noise must
// not generate beats at any level.
static void test_no_false_beats_on_ambient_noise() {
    const double levels[] = {1e-4, 1e-3, 3e-3, 1e-2, 3e-2, 1e-1};
    for (double lvl : levels) {
        int beats = count_beats(0, 0, lvl, 0.0, TICKS_20S);
        double rate = beats / SECONDS_20S;
        // The broken detector produced 3.85-3.95 beats/s here.
        TEST_ASSERT_TRUE_MESSAGE(rate < 0.5,
            "ambient noise must not be detected as a beat");
    }
}

// A microphone DC offset must not hold the noise gate open.
static void test_no_false_beats_on_dc_offset() {
    const double offsets[] = {0.002, 0.008, 0.02, 0.05};
    for (double dc : offsets) {
        int beats = count_beats(0, 0, 1e-4, dc, TICKS_20S);
        TEST_ASSERT_TRUE_MESSAGE(beats / SECONDS_20S < 0.5,
            "DC offset must not be detected as a beat");
    }
}

// Beat tracking must be accurate and, critically, volume-independent: the
// broken version only worked in a narrow loudness window.
static void test_tracks_beats_at_every_volume() {
    const double volumes[] = {0.02, 0.05, 0.1, 0.3, 0.6};
    for (double v : volumes) {
        int beats = count_beats(120.0, v, 3e-3, 0.0, TICKS_20S);
        double rate = beats / SECONDS_20S;
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.25, 2.0, rate,
            "120 BPM must be tracked at ~2.00 beats/s regardless of volume");
    }
}

static void test_tracks_beats_at_every_tempo() {
    const double bpms[] = {60.0, 90.0, 120.0, 140.0, 174.0};
    for (double bpm : bpms) {
        int beats = count_beats(bpm, 0.3, 3e-3, 0.0, TICKS_20S);
        double rate = beats / SECONDS_20S;
        TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.25, bpm / 60.0, rate,
            "tempo must be tracked across the usable BPM range");
    }
}

// When the music stops the pulse must reach full black, otherwise PulseBeat
// stays permanently lit, which is the other half of the reported symptom.
static void test_pulse_reaches_black_after_music_stops() {
    beat_detector_init();
    static int32_t buf[SAMPLES];
    uint64_t phase = 0;
    int t = 0;
    int half = TICKS_20S / 2;

    for (; t < half; t++) {
        test_millis_value = (unsigned long)t * AUDIO_TICK_MS;
        generate(buf, 120.0, 0.3, 3e-3, 0.0, phase);
        phase += SAMPLES;
        beat_detect(analyze(buf));
    }

    float max_decay_after_silence = 0;
    for (; t < TICKS_20S; t++) {
        test_millis_value = (unsigned long)t * AUDIO_TICK_MS;
        generate(buf, 0, 0, 1e-2, 0.0, phase);   // room noise, no music
        phase += SAMPLES;
        BeatState s = beat_detect(analyze(buf));
        // Skip the tail of the last real beat fading out
        if (t > half + 100) max_decay_after_silence = std::max(max_decay_after_silence, s.decay);
    }
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, max_decay_after_silence,
        "PulseBeat must fade to black once the music stops");
}

// Band magnitudes must stay in the documented gain-normalized range so that
// consumers can rely on the scale. The original code fed raw FFT magnitudes
// (bass up to ~59) into consumers that assumed 0..1.
static void test_band_magnitudes_are_gain_normalized() {
    static int32_t buf[SAMPLES];
    generate(buf, 0, 0, 1e-4, 0.0, 0);           // near silence
    TEST_ASSERT_TRUE_MESSAGE(analyze(buf).bass < 1e-3f,
        "silence must produce a near-zero bass magnitude");

    float loudest = 0;
    uint64_t phase = 0;
    for (int t = 0; t < 200; t++) {
        generate(buf, 120.0, 0.6, 3e-3, 0.0, phase);
        phase += SAMPLES;
        loudest = std::max(loudest, analyze(buf).bass);
    }
    TEST_ASSERT_TRUE_MESSAGE(loudest < 1.5f,
        "a full-scale band must read near 1.0, not in the tens");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_false_beats_on_ambient_noise);
    RUN_TEST(test_no_false_beats_on_dc_offset);
    RUN_TEST(test_tracks_beats_at_every_volume);
    RUN_TEST(test_tracks_beats_at_every_tempo);
    RUN_TEST(test_pulse_reaches_black_after_music_stops);
    RUN_TEST(test_band_magnitudes_are_gain_normalized);
    return UNITY_END();
}
