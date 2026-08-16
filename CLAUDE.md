# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and Upload

This is a PlatformIO project targeting **ESP32-S3-DevKitC-1** with Arduino framework.

```bash
pio run                          # Compile
pio run --target upload          # Flash to device
pio run --target upload --target monitor  # Flash + open serial monitor
pio device monitor               # Serial monitor only (115200 baud)
```

No test framework is configured yet. Unit tests would go in `test/` and run with `pio test`.

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

- **`src/audio/`** -- Signal processing pipeline. `audio_capture` reads raw I2S DMA buffers. `fft_processor` computes magnitude spectrum and extracts 4 frequency bands (bass/lowMid/mid/high). `beat_detector` uses spectral flux with adaptive threshold for onset detection.
- **`src/led/`** -- LED output. `led_controller` wraps FastLED with power management (1500mA cap). `patterns.h/cpp` defines the `Pattern` base class and all pattern implementations. New patterns implement `update()` (receive audio data) and `render()` (write to LED array).
- **`src/modes/`** -- State machine (AUDIO_REACTIVE / BLE_SYNC / MANUAL) and pattern registry. `mode_manager` owns the pattern instances and handles cycling.
- **`src/config.h`** -- All pin assignments, timing constants, and tunables in one place.

### Adding a New Pattern

1. Subclass `Pattern` in `src/led/patterns.h` (declare) and `src/led/patterns.cpp` (implement)
2. Increment `NUM_PATTERNS` in `patterns.h`
3. Add an instance to the `patterns[]` array in `mode_manager.cpp`

### Hardware Configuration

All pin mappings are in `src/config.h`. Key pins: GPIO 38 (LED data), GPIO 15/16/17 (I2S mic), GPIO 4/5 (buttons with internal pull-up).

### Hardware Documentation

- `docs/wiring-diagram.html` -- Visual SVG diagram (open in browser), color-coded wires, component layout, and wiring notes
- `docs/schematic.kicad_sch` -- KiCad schematic with all components, nets, and power section for wearable build. Requires community libraries for INMP441 and ESP32-S3-DevKitC symbols.

### Wokwi Simulator Notes

- `diagram.json` uses `board-esp32-devkit-c-v4` (plain ESP32) as a stand-in; the real target is `esp32-s3-devkitc-1`. Before flashing real hardware, restore `LED_PIN` to 38 in `config.h`.
- The `[env:wokwi]` build targets `esp32dev` and defines `SIMULATE_AUDIO=1` (bypasses I2S and FFT, generates synthetic band values directly) and `BTN_LONG_PRESS_MS=5000` (simulation runs slower than real time, so button press durations are inflated).

## Future Modules (not yet implemented)

- `src/ble/` -- NimBLE passive scanner for concert lightstick sync
- `src/web/` -- ESP32 WiFi AP + async web server for phone control
