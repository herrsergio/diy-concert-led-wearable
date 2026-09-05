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
// Boot default only, NOT a cap. The web slider and the button ladder both go
// above this; BRIGHTNESS_LIMIT below is the value actually enforced.
#define MAX_BRIGHTNESS    128       // 0-255, first-boot brightness (~50%)

// Enforced ceiling, applied inside led_set_brightness(). 255 makes the guard a
// no-op today, which is deliberate: the real hardware protection is the FastLED
// power cap in led_controller_init(), which scales the whole strip down on its
// own. This constant exists so there is ONE named place to trade brightness for
// battery life. Lowering it below 230 also requires fixing the button ladder in
// handle_buttons(), which resets only once it reaches 200.
#define BRIGHTNESS_LIMIT  255

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
//
// Coupled with BEAT_CREST_FACTOR below: lowering the crest factor requires
// raising this to keep the false-beat rate on loud room noise under the limit
// test_no_false_beats_on_ambient_noise asserts. See the table there.
#ifndef BEAT_SENSITIVITY_SIGMA
#define BEAT_SENSITIVITY_SIGMA 3.0f
#endif

// A beat also requires the instantaneous bass to exceed the slow running
// average by this factor. Stationary noise has a low crest factor and fails
// this; a kick drum has a high crest factor and passes.
//
// The reference is an EMA of the bass band ITSELF, so the test is
// self-normalizing: during continuous music the reference rises to track the
// music's own bassline, and a kick buried in that bassline has to beat twice
// its own one-second average. That property makes this factor the prime
// suspect whenever PulseBeat responds to hand claps but not to music, and on
// synthetic signals the case against it looks conclusive.
//
// Lowering it was measured and then REJECTED. The real cause of that symptom
// was a damaged microphone; with a healthy INMP441 the patterns track music
// correctly at 2.0. The synthetic kick used for the measurements is a pure
// 80 Hz sine, a harsher case for a self-normalizing gate than real music, so
// the "kick == bassline" column below overstates the problem. Diagnose the
// capture path with `pio run -e probe` BEFORE any detector tunable.
//
// If on-device evidence ever does implicate this gate, the two tunables must
// move together (90 s per run, beats/s):
//
//   sigma  crest | ambient   dense mix  kick == bassline
//   (want)       |  < 0.50      2.00          2.00
//     3.0   2.0  |   0.20       1.83          0.03      <- shipped
//     3.0   1.3  |   1.35       1.97          1.57      fails ambient
//     4.0   2.0  |   0.25       1.53          0.03
//     4.0   1.3  |   0.43       1.83          1.33
//     4.5   1.3  |   0.45       1.63          1.23      music degrades
//
// The evidence to look for is `crestkill` on the `[GATE]` serial line: the
// share of frames where the noise floor and the flux threshold both passed and
// only this test rejected. Staying high while music plays is the signature.
#ifndef BEAT_CREST_FACTOR
#define BEAT_CREST_FACTOR     2.0f
#endif
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
// NOTE: this password is committed to the repository. For a hobby LED strip the
// practical worst case is a stranger at the venue changing your colours, but do
// not reuse it anywhere that matters.
#define WIFI_SSID         "SKZ-LED-Strip"
#define WIFI_PASSWORD     "straykids"

// mDNS hostname, reachable as http://<WIFI_HOSTNAME>.local from a phone joined
// to the AP. mDNS is link-local multicast, so it needs no router and no uplink.
// Mobile browser .local support varies, so web_server_init() also prints the raw
// AP address to serial as the guaranteed fallback.
#define WIFI_HOSTNAME     "skzled"

// HTTP port and the core the web server task is pinned to. Core 0 is where the
// lwIP TCP/IP task already lives (CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0), while
// the Arduino loopTask with the audio and LED ticks runs on core 1
// (CONFIG_ARDUINO_RUNNING_CORE=1). Keeping HTTP off core 1 is what protects the
// 23.22 ms I2S deadline.
#define WEB_SERVER_PORT   80
#define WEB_TASK_CORE     0
#define WEB_TASK_STACK    4096
#define WEB_CMD_QUEUE_LEN 8

// --- Settings persistence (NVS) ---
// Mode, pattern and brightness are written back this long after the LAST change,
// so dragging the brightness slider produces one flash write instead of dozens.
#define SETTINGS_NAMESPACE      "ledstrip"
#define SETTINGS_SAVE_DEBOUNCE_MS 5000

// --- BLE ---
#define BLE_SCAN_INTERVAL 100       // ms between scan windows
#define BLE_SCAN_WINDOW   50        // ms active scanning per interval
#define BLE_TIMEOUT_MS    5000      // Fallback to audio if no BLE signal
