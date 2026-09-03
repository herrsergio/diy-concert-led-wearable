#pragma once

// --- LED Strip Configuration ---
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

#define BEAT_MIN_INTERVAL_MS  200   // Max ~300 BPM
#define BEAT_SENSITIVITY      1.5f  // Adaptive threshold multiplier
#define BEAT_HISTORY_SIZE     43    // ~1 second of FFT frames at 43Hz
#define BEAT_NOISE_FLOOR      0.15f // Minimum bass level to attempt beat detection

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
