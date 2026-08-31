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

static const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = SAMPLES,
    .use_apll = false,
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

size_t audio_capture_read(int32_t* buffer, size_t num_samples) {
    size_t bytes_read = 0;
    i2s_read(I2S_PORT, buffer, num_samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    return bytes_read / sizeof(int32_t);
}

#endif
