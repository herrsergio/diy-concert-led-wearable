#include "fft_processor.h"
#include "../config.h"
#include <math.h>

// --- Per-band visual auto-gain (shared by the real and simulated paths) ---
//
// Maps each band from its own running [floor..peak] window into 0..1. Dividing
// by a running peak alone is not enough: with only noise present the level sits
// at the peak, so every band would read ~1.0 and the strip would stay lit in a
// quiet room. Tracking the floor too, and requiring a minimum peak/floor ratio,
// makes a band with no dynamics collapse to 0.

static float agc_level[BAND_COUNT];
static float agc_peak[BAND_COUNT];
static float agc_floor[BAND_COUNT];

static void auto_gain_reset() {
    for (int i = 0; i < BAND_COUNT; i++) {
        agc_level[i] = 0.0f;
        agc_peak[i] = 0.0f;
        agc_floor[i] = 1.0f;
    }
}

static void auto_gain_apply(FrequencyBands& bands) {
    const float in[BAND_COUNT] = { bands.bass, bands.low_mid, bands.mid, bands.high };

    for (int i = 0; i < BAND_COUNT; i++) {
        // Exponential moving average smooths frame-to-frame jitter
        agc_level[i] = agc_level[i] * 0.7f + in[i] * 0.3f;

        // Peak decays slowly, floor rises slowly, so the window tracks the room
        agc_peak[i] = fmaxf(agc_level[i], agc_peak[i] * BAND_AGC_PEAK_DECAY);
        agc_floor[i] = fminf(agc_level[i], agc_floor[i] * BAND_AGC_FLOOR_RISE + 1e-9f);

        if (agc_peak[i] < 1e-6f) agc_peak[i] = 1e-6f;
        if (agc_floor[i] < 1e-9f) agc_floor[i] = 1e-9f;

        if (agc_peak[i] < agc_floor[i] * BAND_AGC_MIN_RATIO) {
            // No dynamic range in this band: it is noise, not music
            bands.norm[i] = 0.0f;
        } else {
            float span = agc_peak[i] - agc_floor[i];
            float v = (agc_level[i] - agc_floor[i]) / span;
            bands.norm[i] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        }
    }
}

#ifdef SIMULATE_AUDIO

#include <Arduino.h>

void fft_processor_init() {
    auto_gain_reset();
}

FrequencyBands fft_process(const int32_t* samples, size_t num_samples) {
    unsigned long ms = millis();
    float beat_phase = fmod(ms / 460.0f, 1.0f);
    float kick = (beat_phase < 0.08f) ? (1.0f - beat_phase / 0.08f) : 0.0f;
    float hat_phase = fmod((ms + 230) / 460.0f, 1.0f);
    float hat = (hat_phase < 0.03f) ? 0.3f : 0.0f;
    float mid_mod = 0.3f * (0.5f + 0.5f * sinf(2.0f * M_PI * 2.0f * ms / 1000.0f));

    FrequencyBands bands = {};
    bands.bass    = kick * 0.9f;
    bands.low_mid = kick * 0.3f + 0.05f;
    bands.mid     = mid_mod;
    bands.high    = hat;
    bands.overall = (bands.bass + bands.low_mid + bands.mid + bands.high) / 4.0f;
    auto_gain_apply(bands);
    return bands;
}

#else

#include <arduinoFFT.h>

// Single precision, not double. The Xtensa LX6 in the ESP32-D0WD-V3 has a
// single-precision FPU only, so every double operation in a 1024-point FFT is
// software-emulated. That put the audio tick at ~33 ms against the 23.22 ms the
// microphone needs to produce one window, so the reader fell permanently behind
// and the I2S driver discarded ~30% of all audio. See the overflow diagnostics
// in audio_capture.cpp. ArduinoFFT<float> is explicitly instantiated by the
// library (arduinoFFT.cpp: template class ArduinoFFT<float>).
static float v_real[SAMPLES];
static float v_imag[SAMPLES];
static ArduinoFFT<float> fft(v_real, v_imag, SAMPLES, SAMPLE_RATE);

// Returns the mean magnitude over [freq_low, freq_high), divided by the FFT
// coherent gain so the result is comparable to input amplitude rather than
// being an arbitrary function of SAMPLES and the window.
static float band_energy(uint16_t freq_low, uint16_t freq_high) {
    const float bin_width = (float)SAMPLE_RATE / SAMPLES;
    int bin_low = (int)(freq_low / bin_width);
    int bin_high = (int)(freq_high / bin_width);

    // Bin 0 is DC and bin 1 still carries window leakage from any residual
    // offset, so the usable spectrum starts at bin 1.
    if (bin_low < 1) bin_low = 1;
    if (bin_high > SAMPLES / 2) bin_high = SAMPLES / 2;
    // Half-open at the top so adjacent bands do not share a bin
    if (bin_high <= bin_low) bin_high = bin_low + 1;

    float energy = 0;
    for (int i = bin_low; i < bin_high; i++) {
        energy += (float)v_real[i];
    }
    return (energy / (bin_high - bin_low)) / FFT_MAG_SCALE;
}

void fft_processor_init() {
    auto_gain_reset();
}

FrequencyBands fft_process(const int32_t* samples, size_t num_samples) {
    // Convert I2S 32-bit samples to float, normalize.
    // INMP441 outputs 24-bit data left-aligned in a 32-bit word.
    for (size_t i = 0; i < num_samples && i < SAMPLES; i++) {
        v_real[i] = (float)(samples[i] >> 8) / 8388608.0f;
        v_imag[i] = 0.0;
    }

    // Strip the DC offset first. Without this, the window smears the offset
    // into the lowest bins and permanently inflates the bass band, which keeps
    // the beat detector's noise gate open even in a silent room.
    fft.dcRemoval();

    fft.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    fft.compute(FFTDirection::Forward);
    fft.complexToMagnitude();

    FrequencyBands bands = {};
    bands.bass    = band_energy(FFT_BAND_BASS_LOW, FFT_BAND_BASS_HIGH);
    bands.low_mid = band_energy(FFT_BAND_BASS_HIGH, FFT_BAND_LOWMID_HIGH);
    bands.mid     = band_energy(FFT_BAND_LOWMID_HIGH, FFT_BAND_MID_HIGH);
    bands.high    = band_energy(FFT_BAND_MID_HIGH, FFT_BAND_HIGH_HIGH);
    bands.overall = (bands.bass + bands.low_mid + bands.mid + bands.high) / 4.0f;

    auto_gain_apply(bands);
    return bands;
}

#endif
