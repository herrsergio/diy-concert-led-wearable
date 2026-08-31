#include "fft_processor.h"
#include "../config.h"

#ifdef SIMULATE_AUDIO

#include <Arduino.h>
#include <math.h>

void fft_processor_init() {}

FrequencyBands fft_process(const int32_t* samples, size_t num_samples) {
    unsigned long ms = millis();
    float beat_phase = fmod(ms / 460.0f, 1.0f);
    float kick = (beat_phase < 0.08f) ? (1.0f - beat_phase / 0.08f) : 0.0f;
    float hat_phase = fmod((ms + 230) / 460.0f, 1.0f);
    float hat = (hat_phase < 0.03f) ? 0.3f : 0.0f;
    float mid_mod = 0.3f * (0.5f + 0.5f * sinf(2.0f * M_PI * 2.0f * ms / 1000.0f));

    FrequencyBands bands;
    bands.bass    = kick * 0.9f;
    bands.low_mid = kick * 0.3f + 0.05f;
    bands.mid     = mid_mod;
    bands.high    = hat;
    bands.overall = (bands.bass + bands.low_mid + bands.mid + bands.high) / 4.0f;
    return bands;
}

#else

#include <arduinoFFT.h>

static double v_real[SAMPLES];
static double v_imag[SAMPLES];
static ArduinoFFT<double> fft(v_real, v_imag, SAMPLES, SAMPLE_RATE);

static float band_energy(uint16_t freq_low, uint16_t freq_high) {
    float bin_width = (float)SAMPLE_RATE / SAMPLES;
    uint16_t bin_low = freq_low / bin_width;
    uint16_t bin_high = freq_high / bin_width;

    if (bin_low < 1) bin_low = 1;
    if (bin_high > SAMPLES / 2) bin_high = SAMPLES / 2;

    float energy = 0;
    for (uint16_t i = bin_low; i <= bin_high; i++) {
        energy += v_real[i];
    }
    return energy / (bin_high - bin_low + 1);
}

void fft_processor_init() {
    // Nothing to initialize; buffers are static
}

FrequencyBands fft_process(const int32_t* samples, size_t num_samples) {
    // Convert I2S 32-bit samples to double, normalize
    for (size_t i = 0; i < num_samples && i < SAMPLES; i++) {
        // INMP441 outputs 24-bit data left-aligned in 32-bit word
        v_real[i] = (double)(samples[i] >> 8) / 8388608.0;
        v_imag[i] = 0.0;
    }

    fft.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    fft.compute(FFTDirection::Forward);
    fft.complexToMagnitude();

    FrequencyBands bands;
    bands.bass    = band_energy(FFT_BAND_BASS_LOW, FFT_BAND_BASS_HIGH);
    bands.low_mid = band_energy(FFT_BAND_BASS_HIGH, FFT_BAND_LOWMID_HIGH);
    bands.mid     = band_energy(FFT_BAND_LOWMID_HIGH, FFT_BAND_MID_HIGH);
    bands.high    = band_energy(FFT_BAND_MID_HIGH, FFT_BAND_HIGH_HIGH);
    bands.overall = (bands.bass + bands.low_mid + bands.mid + bands.high) / 4.0f;

    return bands;
}

#endif
