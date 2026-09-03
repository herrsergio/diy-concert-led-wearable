# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and Upload

This is a PlatformIO project targeting an **ESP32-WROOM-32** module (**ESP32-D0WD-V3** chip, dual core, 4MB flash) with the Arduino framework. The PlatformIO board id is `esp32dev`.

```bash
pio run                          # Compile (default env: esp32wroom32)
pio run --target upload          # Flash to device
pio run --target upload --target monitor  # Flash + open serial monitor
pio device monitor               # Serial monitor only (115200 baud)
```

Environments in `platformio.ini`:

| Env | Purpose |
|---|---|
| `esp32wroom32` | Real hardware. The default env, so a bare `pio run` builds it. |
| `probe` | Real hardware plus `-DI2S_PROBE`, the I2S frame diagnostic. |
| `wokwi` | Simulator build, synthetic audio instead of I2S. |
| `native` | Host-side unit tests. |

### Diagnosing the microphone

If the bands barely move when music plays, check the capture path before touching any detector tunable. The failure mode to rule out first: with L/R tied to a fixed level the INMP441 drives only one half of the I2S frame, and reading the undriven half gives low-level values that wander at low frequency and do not respond to sound, which is easy to mistake for a working but quiet microphone.

```bash
pio run -e probe --target upload --target monitor
```

This reads the full stereo frame and prints `[PROBE] ch0 peak=... dc=... | ch1 peak=...` once per second. Clap next to the mic: the half whose peak jumps is the one carrying audio, and `AUDIO_MIC_CHANNEL_FMT` in `audio_capture.cpp` must select it. If neither half responds, the fault is upstream: wiring, the SD line, the 3.3V supply, or the microphone itself.

The tell in a serial capture is that `mid` and `high` are identical with and without music. Acoustic sound moves every band; variation confined to `bass` alone is low-frequency drift on an input that is not carrying signal.

### Tests

```bash
pio test -e native               # Host-side audio pipeline tests
```

`test/test_beat_detector/` drives the real `beat_detector` against synthetic silence, noise and beats, asserting false-beat rates and BPM tracking as numbers. The FFT stage is mirrored locally in the test because arduinoFFT and the ESP32 I2S driver are not available on the host, so a change to `fft_processor.cpp` band extraction or normalization must be mirrored in `test_beat_detector.cpp`. `test/stubs/Arduino.h` stubs `millis()` via `test_millis_value` so tests advance time in exact `AUDIO_TICK_MS` steps.

## Architecture

The firmware runs two timed loops inside `loop()`:
- **Audio tick** (~43Hz / 23ms): captures I2S samples, runs FFT, detects beats
- **LED render tick** (~60 FPS / 16ms): updates the active pattern and calls `FastLED.show()`

### Data Flow

```
INMP441 mic (I2S) -> audio_capture -> fft_processor -> beat_detector
                                                           |
                                                           v
                              mode_manager -> Pattern::update() -> Pattern::render() -> FastLED
```

### Module Responsibilities

- **`src/audio/`** -- Signal processing pipeline. `audio_capture` reads raw I2S DMA buffers. `fft_processor` computes the magnitude spectrum and extracts 4 frequency bands (bass/lowMid/mid/high). `beat_detector` uses spectral flux with an adaptive threshold for onset detection.

### Audio Signal Contract

`FrequencyBands` carries each band at two different scales. Using the wrong one is the bug class that made PulseBeat and FrequencyBars light up with no music playing.

- `bass` / `low_mid` / `mid` / `high` / `overall` are **gain-normalized FFT magnitudes**, divided by `FFT_MAG_SCALE` so a full-scale sine reads about 1.0 in its band. In practice music sits far below 1.0, so these are for detection logic and diagnostics, not for driving brightness.
- `norm[BAND_COUNT]`, indexed by the `Band` enum, is **auto-gained 0.0 to 1.0** per band. Patterns should use this. Each band is mapped from its own running floor/peak window, and a band whose peak/floor ratio falls below `BAND_AGC_MIN_RATIO` is forced to 0, which is what keeps the strip dark in a quiet room.

`beat_detector` thresholds flux at `mean + BEAT_SENSITIVITY_SIGMA * stddev`, not `mean * k`. A plain multiplier fires on roughly half of all frames for any stationary signal, which the minimum-interval limiter turns into a false-beat metronome. It also requires a crest test (`bass > slow_avg * BEAT_CREST_FACTOR`), which is what actually rejects a noisy room. `BEAT_NOISE_FLOOR` only rejects near-digital silence: raising it cannot fix false beats, because the bass level of a noisy room overlaps the bass level of quiet music.

`main.cpp` clears `beat_detected` after each render, because the 16ms render tick would otherwise see one beat on two consecutive frames.
- **`src/led/`** -- LED output. `led_controller` wraps FastLED with power management (1500mA cap). `patterns.h/cpp` defines the `Pattern` base class and all pattern implementations. New patterns implement `update()` (receive audio data) and `render()` (write to LED array).
- **`src/modes/`** -- State machine (AUDIO_REACTIVE / BLE_SYNC / MANUAL) and pattern registry. `mode_manager` owns the pattern instances and handles cycling.
- **`src/config.h`** -- All pin assignments, timing constants, and tunables in one place.

### Adding a New Pattern

1. Subclass `Pattern` in `src/led/patterns.h` (declare) and `src/led/patterns.cpp` (implement)
2. Increment `NUM_PATTERNS` in `patterns.h`
3. Add an instance to the `patterns[]` array in `mode_manager.cpp`

### Hardware Configuration

All pin mappings are in `src/config.h`. Key pins: GPIO 2 (LED data), GPIO 15/16/17 (I2S mic), GPIO 4/5 (buttons with internal pull-up).

WROOM-32 constraints worth remembering when reassigning pins:

- GPIO 34-39 are **input only**, so they cannot drive the LED strip.
- GPIO 16/17 are free here because WROOM-32 has no PSRAM. They are unavailable on WROVER modules.
- GPIO 2 is a boot strapping pin. It works for LED data, but if uploads start failing, disconnect the strip's data line while flashing or move `LED_PIN` to GPIO 13, 18 or 19.
- The INMP441 L/R pin is tied to GND and `audio_capture.cpp` uses `I2S_CHANNEL_FMT_ONLY_RIGHT`. This was determined empirically: `ONLY_LEFT` returned silence on this wiring. Do not "correct" it.

### Hardware Documentation

- `docs/wiring-diagram.html` -- Visual SVG diagram (open in browser), color-coded wires, component layout, and wiring notes
- `docs/schematic.kicad_sch` -- KiCad schematic with all components, nets, and power section for wearable build. Requires community libraries for the INMP441 and ESP32 module symbols. Note this schematic still carries the original ESP32-S3-DevKitC symbol and has not been redrawn for WROOM-32.

### Wokwi Simulator Notes

- `diagram.json` uses `board-esp32-devkit-c-v4`, which matches the real WROOM-32 target, and drives the strip on GPIO 2 as the firmware does. No pin swap is needed before flashing real hardware.
- The `[env:wokwi]` build defines `SIMULATE_AUDIO=1` (bypasses I2S and FFT, generates synthetic band values directly, then runs them through the same auto-gain as the real path) and `BTN_LONG_PRESS_MS=5000` (simulation runs slower than real time, so button press durations are inflated).

## Future Modules (not yet implemented)

- `src/ble/` -- NimBLE passive scanner for concert lightstick sync
- `src/web/` -- ESP32 WiFi AP + async web server for phone control
