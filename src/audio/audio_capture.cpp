#include "audio_capture.h"
#include "../config.h"

#ifdef SIMULATE_AUDIO

#include <Arduino.h>
#include <math.h>

static uint32_t sim_phase = 0;

void audio_capture_init() {
    sim_phase = 0;
}

size_t audio_capture_read(int32_t* buffer, size_t num_samples) {
    // Generate a synthetic signal: bass kick at ~130 BPM (460ms period)
    // plus mid-frequency content for visual variety
    unsigned long ms = millis();
    float beat_phase = fmod(ms / 460.0f, 1.0f);
    // Sharp transient at the start of each beat cycle
    float kick = (beat_phase < 0.08f) ? (1.0f - beat_phase / 0.08f) : 0.0f;

    for (size_t i = 0; i < num_samples; i++) {
        float t = (float)(sim_phase + i) / SAMPLE_RATE;
        // Bass: 80Hz sine shaped by kick envelope
        float bass = kick * sinf(2.0f * M_PI * 80.0f * t);
        // Mid content: 1200Hz modulated slowly
        float mid = 0.3f * sinf(2.0f * M_PI * 1200.0f * t) *
                    (0.5f + 0.5f * sinf(2.0f * M_PI * 2.0f * t));
        // High hat: noise burst offset from kick
        float hat_phase = fmod((ms + 230) / 460.0f, 1.0f);
        float hat = (hat_phase < 0.03f) ? 0.2f * (((sim_phase + i) * 1103515245 + 12345) % 1000 / 500.0f - 1.0f) : 0.0f;

        float sample = bass + mid + hat;
        // Scale to 24-bit I2S format (left-aligned in 32-bit)
        buffer[i] = (int32_t)(sample * 4000000.0f);
    }
    sim_phase += num_samples;
    return num_samples;
}

#else

#include <driver/i2s.h>
#include <Arduino.h>

// Which half of the I2S frame the microphone data is taken from. ONLY_RIGHT was
// chosen empirically because ONLY_LEFT returned zeros, but see the I2S_PROBE
// notes in config.h: reading the half the microphone does not drive also
// produces a plausible-looking signal, so verify with the probe before trusting
// this. In probe mode the full stereo frame is read instead.
#ifdef I2S_PROBE
#define AUDIO_MIC_CHANNEL_FMT  I2S_CHANNEL_FMT_RIGHT_LEFT
#else
#define AUDIO_MIC_CHANNEL_FMT  I2S_CHANNEL_FMT_ONLY_RIGHT
#endif

static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = AUDIO_MIC_CHANNEL_FMT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = SAMPLES,
    .use_apll = true,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
};

static const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_PIN
};

void audio_capture_init() {
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

#ifdef I2S_PROBE

// Diagnostic read: pull the whole stereo frame so both halves can be compared
// side by side. Peaks are printed in the same raw units as [MIC] max_raw, where
// full scale is 2147483392 (24-bit sample left-aligned in 32 bits).
static int32_t probe_buffer[SAMPLES * 2];
static uint16_t probe_counter = 0;

size_t audio_capture_read(int32_t* buffer, size_t num_samples) {
    size_t want = num_samples * 2 * sizeof(int32_t);
    if (want > sizeof(probe_buffer)) want = sizeof(probe_buffer);

    size_t bytes_read = 0;
    i2s_read(I2S_PORT, probe_buffer, want, &bytes_read, portMAX_DELAY);

    size_t frames = (bytes_read / sizeof(int32_t)) / 2;

    int32_t peak_a = 0, peak_b = 0;
    int64_t sum_a = 0, sum_b = 0;

    for (size_t i = 0; i < frames; i++) {
        int32_t a = probe_buffer[i * 2];
        int32_t b = probe_buffer[i * 2 + 1];

        sum_a += a;
        sum_b += b;

        int32_t abs_a = a < 0 ? -a : a;
        int32_t abs_b = b < 0 ? -b : b;
        if (abs_a > peak_a) peak_a = abs_a;
        if (abs_b > peak_b) peak_b = abs_b;

        // Forward the selected half so the FFT and patterns keep running
        buffer[i] = (I2S_PROBE_CHANNEL == 0) ? a : b;
    }

    // One line per second at the ~43Hz audio tick rate
    if (++probe_counter >= 43) {
        probe_counter = 0;
        long dc_a = frames ? (long)(sum_a / (int64_t)frames) : 0;
        long dc_b = frames ? (long)(sum_b / (int64_t)frames) : 0;
        Serial.printf("[PROBE] ch0 peak=%10ld dc=%10ld | ch1 peak=%10ld dc=%10ld  (forwarding ch%d)\n",
                      (long)peak_a, dc_a, (long)peak_b, dc_b, I2S_PROBE_CHANNEL);
    }

    return frames;
}

#else

size_t audio_capture_read(int32_t* buffer, size_t num_samples) {
    size_t bytes_read = 0;
    i2s_read(I2S_PORT, buffer, num_samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    return bytes_read / sizeof(int32_t);
}

#endif  // I2S_PROBE

#endif  // SIMULATE_AUDIO
