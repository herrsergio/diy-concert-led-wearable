#pragma once

// Target: ESP32-WROOM-32 module (ESP32-D0WD-V3). Pin choices below are valid
// for that module: GPIO 34-39 are input-only, and GPIO 16/17 are free because
// WROOM-32 has no PSRAM (they are taken on WROVER modules).

// --- LED Strip Configuration ---
// GPIO 2 is a boot strapping pin. It works as LED data here, but if uploads
// ever start failing, disconnect the strip's data line while flashing or move
// this to a non-strapping pin such as GPIO 13, 18 or 19.
#define LED_PIN           2
#define NUM_LEDS          60
#define LED_TYPE          WS2812B
#define COLOR_ORDER       GRB
#define MAX_BRIGHTNESS    128       // 0-255, capped for battery life (~50%)
#define MAX_POWER_MW      7500      // 5V * 1500mA = 7.5W power budget

// --- I2S Microphone (INMP441) ---
#define I2S_WS_PIN        15        // Word Select (LRCLK)
#define I2S_SCK_PIN       16        // Serial Clock (BCLK)
#define I2S_SD_PIN        17        // Serial Data (DOUT)
#define I2S_PORT          I2S_NUM_0
#define SAMPLE_RATE       44100
#define SAMPLES           1024      // FFT window size (must be power of 2)

// DMA ring geometry. The legacy I2S driver enforces a HARD limit of 4092 bytes
// per DMA buffer: real_dma_buf_size = dma_buf_len * channels * bits/8. It does
// not reject an oversized request -- i2s_check_cfg_validity() only rejects
// dma_buf_len outside 8..1024, and i2s_get_buf_size() then SILENTLY clamps it
// (esp-idf v4.4.7, components/driver/i2s.c:686-694). dma_buf_len = SAMPLES was
// therefore clamped to 1023 frames mono and 511 in the stereo probe build.
// 256 frames is legal in both: 256 * 2 * 4 = 2048 bytes at worst.
#define I2S_DMA_BUF_LEN   256       // frames per DMA buffer (<= 511 for stereo)
#define I2S_DMA_BUF_COUNT 8         // 8 * 256 = 2048 frames = 46.4 ms of ring

// --- I2S Frame Diagnostics ---
// With L/R tied to a fixed level the INMP441 drives microphone data during only
// one half of the I2S frame and leaves the bus undriven during the other half.
// Selecting the undriven half yields low-level values that wander at low
// frequency and do not respond to sound at all, which looks deceptively like a
// working but very quiet microphone.
//
// Build with -DI2S_PROBE (or `pio run -e probe`) to read the whole stereo frame
// and print the peak and DC level of both halves once per second. Clap or
// whistle next to the microphone: the half whose peak jumps by a large factor is
// the one carrying audio, and that is the half AUDIO_MIC_CHANNEL_FMT must
// select in audio_capture.cpp. If neither half responds, the fault is upstream
// of this setting: wiring, the SD line, the 3.3V supply, or the microphone.
//
// I2S_PROBE_CHANNEL picks which half is forwarded to the FFT while probing, so
// the patterns keep running. 0 is the first sample of each frame pair.
#ifndef I2S_PROBE_CHANNEL
#define I2S_PROBE_CHANNEL 0
#endif

// --- Buttons ---
#define BTN_MODE_PIN      4         // Pattern/mode cycle button
#define BTN_BRIGHT_PIN    5         // Brightness adjust button
#ifndef BTN_DEBOUNCE_MS
#define BTN_DEBOUNCE_MS   50
#endif
#ifndef BTN_LONG_PRESS_MS
#define BTN_LONG_PRESS_MS 1000
#endif

// --- Audio Processing ---
#define FFT_BAND_BASS_LOW     20
#define FFT_BAND_BASS_HIGH    250
#define FFT_BAND_LOWMID_HIGH  1000
#define FFT_BAND_MID_HIGH     4000
#define FFT_BAND_HIGH_HIGH    16000

// Band magnitudes are divided by the FFT coherent gain so a full-scale sine
// reads ~1.0 in its band. Hamming window coherent gain = 0.54.
#define FFT_COHERENT_GAIN     0.54f
#define FFT_MAG_SCALE         ((SAMPLES / 2.0f) * FFT_COHERENT_GAIN)

#define BEAT_MIN_INTERVAL_MS  200   // Max ~300 BPM
#define BEAT_HISTORY_SIZE     43    // ~1 second of FFT frames at 43Hz

// Onset threshold = mean(flux) + BEAT_SENSITIVITY_SIGMA * stddev(flux).
// This is a *shape* test, not a loudness test, so it works at any volume.
// Higher = fewer false beats but more missed beats. 3.0 measured best.
#define BEAT_SENSITIVITY_SIGMA 3.0f

// A beat also requires the instantaneous bass to exceed the slow running
// average by this factor. Stationary noise has a low crest factor and fails
// this; a kick drum has a high crest factor and passes. This is what actually
// rejects a noisy room, so tune it before touching anything else.
#define BEAT_CREST_FACTOR     2.0f
#define BEAT_LEVEL_AVG_ALPHA  0.02f // Slow bass average (~1.1s time constant)

// Absolute gate, in gain-normalized units. Only meant to reject near-digital
// silence; discrimination between music and noise is done by the two tests
// above. Do NOT raise this to fight false beats -- the value range for room
// noise overlaps the range for quiet music, so no setting can separate them.
#define BEAT_NOISE_FLOOR      0.0004f

#define BEAT_DECAY_PER_TICK   0.92f // Pulse fade per audio tick (~277ms tau)

// --- Visual Auto-Gain ---
// Each band is mapped from its own running [floor..peak] window into 0..1 so
// patterns stay responsive at any volume. A band whose peak/floor ratio is
// below BAND_AGC_MIN_RATIO carries no dynamics (i.e. it is just noise) and is
// forced to 0, which is what keeps the LEDs dark in a quiet room.
#define BAND_AGC_PEAK_DECAY   0.995f
#define BAND_AGC_FLOOR_RISE   1.02f
#define BAND_AGC_MIN_RATIO    2.0f

// --- Timing ---
#define AUDIO_TICK_MS     23        // ~43Hz audio processing rate
#define LED_RENDER_MS     16        // ~60 FPS LED update rate

// --- WiFi AP ---
#define WIFI_SSID         "SKZ-LED-Strip"
#define WIFI_PASSWORD     "straykids"

// --- BLE ---
#define BLE_SCAN_INTERVAL 100       // ms between scan windows
#define BLE_SCAN_WINDOW   50        // ms active scanning per interval
#define BLE_TIMEOUT_MS    5000      // Fallback to audio if no BLE signal
