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
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
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
    .dma_buf_count = I2S_DMA_BUF_COUNT,
    .dma_buf_len = I2S_DMA_BUF_LEN,
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

// --- I2S health diagnostics ------------------------------------------------
// When the RX ring overflows, the legacy I2S driver discards the OLDEST DMA
// buffer (esp-idf v4.4.7, components/driver/i2s.c, I2S_INTR_IN_SUC_EOF handler),
// so the next i2s_read() returns audio that is not temporally adjacent to the
// previous read. That join is a step discontinuity: broadband and with a high
// crest factor, which is exactly what a spectral-flux onset detector calls a
// drum hit. No BEAT_SENSITIVITY_SIGMA or BEAT_NOISE_FLOOR value can reject it.
//
// The driver reports the event as I2S_EVENT_RX_Q_OVF, but only if it was
// installed with a real event queue instead of (0, NULL).
static QueueHandle_t i2s_evt_queue = NULL;
static uint32_t i2s_ovf_count = 0;   // RX ring overflows since boot (cumulative)
static uint32_t read_calls = 0;
static uint32_t wait_us_max = 0;     // longest block inside i2s_read
static uint64_t wait_us_sum = 0;
static uint32_t gap_us_max = 0;      // longest interval between consecutive reads
static uint64_t gap_us_sum = 0;
static uint32_t last_read_us = 0;
static uint16_t diag_counter = 0;

// Drain the event queue without blocking; only overflows are of interest.
static void i2s_diag_poll() {
    i2s_event_t evt;
    while (i2s_evt_queue && xQueueReceive(i2s_evt_queue, &evt, 0) == pdTRUE) {
        if (evt.type == I2S_EVENT_RX_Q_OVF) i2s_ovf_count++;
    }
}

// i2s_read plus timing. The two numbers together say whether the reader keeps
// up with the microphone, which produces 1024 frames every 23.22 ms:
//   wait_avg near 0 and gap_avg above 23220 us -> a backlog stands permanently,
//                                                 the ring overflows every buffer
//   wait_avg of several ms                     -> reader is self-paced, ring healthy
static size_t i2s_read_timed(void* dest, size_t bytes) {
    uint32_t entry = micros();
    if (last_read_us) {
        uint32_t gap = entry - last_read_us;
        gap_us_sum += gap;
        if (gap > gap_us_max) gap_us_max = gap;
    }
    last_read_us = entry;

    size_t bytes_read = 0;
    i2s_read(I2S_PORT, dest, bytes, &bytes_read, portMAX_DELAY);

    uint32_t wait = micros() - entry;
    wait_us_sum += wait;
    if (wait > wait_us_max) wait_us_max = wait;
    read_calls++;

    i2s_diag_poll();
    return bytes_read;
}

// One line per second at the ~43Hz audio tick rate. Self-gating.
static void i2s_diag_report() {
    if (++diag_counter < 43) return;
    diag_counter = 0;
    uint32_t n = read_calls ? read_calls : 1;
    Serial.printf("[I2S] ovf=%lu reads=%lu wait_avg=%luus wait_max=%luus "
                  "gap_avg=%luus gap_max=%luus\n",
                  (unsigned long)i2s_ovf_count, (unsigned long)read_calls,
                  (unsigned long)(wait_us_sum / n), (unsigned long)wait_us_max,
                  (unsigned long)(gap_us_sum / n), (unsigned long)gap_us_max);
    read_calls = 0;
    wait_us_sum = 0; wait_us_max = 0;
    gap_us_sum = 0;  gap_us_max = 0;
}

void audio_capture_init() {
    // Report the DMA geometry against the driver's hard limit. i2s_check_cfg_
    // validity() only rejects dma_buf_len outside 8..1024, then i2s_get_buf_size()
    // SILENTLY clamps it so that dma_buf_len * chan * bytes_per_sample stays at
    // or below I2S_DMA_BUFFER_MAX_SIZE (4092). A request that passes validation
    // can therefore yield a far shallower ring than asked for.
    const int chan = (AUDIO_MIC_CHANNEL_FMT == I2S_CHANNEL_FMT_RIGHT_LEFT) ? 2 : 1;
    const int bytes_per_buf = i2s_config.dma_buf_len * chan * 4;
    Serial.printf("[I2S] requested dma_buf_len=%d count=%d chan=%d -> %d bytes/buf"
                  " (driver limit 4092)%s\n",
                  i2s_config.dma_buf_len, i2s_config.dma_buf_count, chan,
                  bytes_per_buf,
                  bytes_per_buf > 4092 ? "  *** WILL BE CLAMPED ***" : "");

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 16, &i2s_evt_queue);
    Serial.printf("[I2S] driver_install: %s\n", esp_err_to_name(err));

    err = i2s_set_pin(I2S_PORT, &pin_config);
    Serial.printf("[I2S] set_pin: %s\n", esp_err_to_name(err));

    i2s_zero_dma_buffer(I2S_PORT);
    last_read_us = 0;
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

    size_t bytes_read = i2s_read_timed(probe_buffer, want);

    size_t frames = (bytes_read / sizeof(int32_t)) / 2;

    // Peaks accumulate across the whole reporting window. Resetting them every
    // call meant the printed line described one 23 ms window out of each ~1000,
    // so a clap or any other transient was almost always missed.
    static int32_t peak_a = 0, peak_b = 0;
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

    // One line per 43 audio ticks (~1 s). peak is over the whole window; dc is
    // still the mean of the most recent window only.
    if (++probe_counter >= 43) {
        probe_counter = 0;
        long dc_a = frames ? (long)(sum_a / (int64_t)frames) : 0;
        long dc_b = frames ? (long)(sum_b / (int64_t)frames) : 0;
        Serial.printf("[PROBE] ch0 peak=%10ld (%.3f%%FS) dc=%10ld | ch1 peak=%10ld dc=%10ld  (forwarding ch%d)\n",
                      (long)peak_a, 100.0 * (double)peak_a / 2147483392.0,
                      dc_a, (long)peak_b, dc_b, I2S_PROBE_CHANNEL);
        peak_a = 0;
        peak_b = 0;
    }

    i2s_diag_report();
    return frames;
}

#else

size_t audio_capture_read(int32_t* buffer, size_t num_samples) {
    size_t bytes_read = i2s_read_timed(buffer, num_samples * sizeof(int32_t));
    i2s_diag_report();
    return bytes_read / sizeof(int32_t);
}

#endif  // I2S_PROBE

#endif  // SIMULATE_AUDIO
