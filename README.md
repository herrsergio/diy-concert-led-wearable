# Music LED Strip

A wearable, audio-reactive LED strip built on an ESP32-WROOM-32 that pulses and changes colors with music. Designed to bring the K-pop concert lightstick experience to a DIY LED wearable, specifically targeting Stray Kids concerts.

## What It Does

- **Audio-reactive mode**: An INMP441 microphone captures ambient music, runs FFT analysis, and drives LED patterns synced to the beat
- **BLE concert sync** (planned): Passively sniffs venue BLE broadcasts to mirror official lightstick colors
- **Manual mode** (planned): Phone control via WiFi AP web interface

## Hardware

| Component | Part |
|-----------|------|
| MCU | ESP32-WROOM-32 dev board (ESP32-D0WD-V3 chip, dual core, 4MB flash) |
| LEDs | WS2812B strip, 60 LEDs/m, 1m |
| Microphone | INMP441 I2S MEMS |
| Power (dev) | USB-C 5V |
| Power (wearable) | 18650 Li-Ion + MT3608 boost converter |

### Wiring

Open [`docs/wiring-diagram.html`](docs/wiring-diagram.html) in a browser for the full visual diagram with color-coded wires and component layout.

Quick reference:

| ESP32 Pin | Connects To | Wire Color |
|---|---|---|
| GPIO 2 | WS2812B DIN (through 470 ohm resistor) | Green |
| 5V | WS2812B +5V | Red |
| GND | WS2812B GND, INMP441 GND + L/R, Buttons | Black |
| 3.3V | INMP441 VDD | Yellow |
| GPIO 16 | INMP441 SCK (BCLK) | Blue |
| GPIO 15 | INMP441 WS (LRCLK) | Blue |
| GPIO 17 | INMP441 SD (DOUT) | Blue |
| GPIO 4 | Mode button -> GND | Orange |
| GPIO 5 | Brightness button -> GND | Orange |

Place a 1000uF capacitor across the strip's +5V and GND pins.

The INMP441 L/R pin goes to GND, and the firmware reads `I2S_CHANNEL_FMT_ONLY_RIGHT`. That combination is what produces samples on this wiring; `ONLY_LEFT` returned silence.

GPIO 2 is a boot strapping pin on the ESP32. It works fine as the LED data line, but if flashing ever fails, unplug the strip's data wire while uploading.

## Building and Flashing

Requires [PlatformIO](https://platformio.org/).

```bash
# Install PlatformIO CLI (if not already installed)
pip install platformio

# Compile (default environment is esp32wroom32)
pio run

# Flash and open serial monitor
pio run --target upload --target monitor
```

## Tests

The audio pipeline has host-side regression tests that need no hardware:

```bash
pio test -e native
```

They drive the real beat detector against synthetic silence, room noise and beats at several volumes and tempos, then assert false-beat rates and BPM tracking. These exist because the original detector fired about 4 false beats per second on plain room noise while looking perfectly reasonable in source form.

## Troubleshooting the microphone

If the LEDs react the same way whether or not music is playing, verify the microphone is actually capturing audio before adjusting any beat detection setting:

```bash
pio run -e probe --target upload --target monitor
```

This prints one line per second showing the peak and DC level of both halves of the I2S frame:

```
[PROBE] ch0 peak=   5285376 dc=      1024 | ch1 peak= 218103808 dc=       -64  (forwarding ch0)
```

Clap or whistle next to the microphone. One half should jump by a large factor while the other stays flat. The half that jumps is carrying audio, and `AUDIO_MIC_CHANNEL_FMT` in `src/audio/audio_capture.cpp` must be set to select it. If neither half reacts, the problem is the wiring, the SD line, the 3.3V supply, or the microphone.

Why this matters: with the INMP441's L/R pin tied to a fixed level, the microphone drives data during only one half of the I2S frame and leaves the bus undriven during the other. Reading the undriven half produces low-level values that drift slowly and ignore sound, which looks very much like a working but quiet microphone. Note that `mid` and `high` reading nearly the same with and without music is NOT a reliable tell: `band_energy()` divides each band by its bin count, and `high` spans 279 bins against `bass`'s 4, so real acoustic content is averaged down until it looks flat. Use the probe, not the band values.

## Simulator (Wokwi)

You can test patterns and button behavior without hardware using the [Wokwi simulator](https://wokwi.com/).

```bash
# Build the simulator environment (uses synthetic audio instead of I2S mic)
pio run -e wokwi
```

Then in VS Code with the [Wokwi extension](https://marketplace.visualstudio.com/items?itemName=Wokwi.wokwi-vscode) installed, press F1 and select "Wokwi: Start Simulator".

The simulator environment defines `SIMULATE_AUDIO`, which replaces the I2S microphone input with a synthetic signal: a 130 BPM bass kick + mid-frequency content + hi-hat bursts. This drives all patterns so you can see them animate and test button cycling.

**Simulator layout**: ESP32 DevKit C V4, which matches the real WROOM-32 target, + 10 WS2812B LEDs (two 5-LED strips chained on GPIO 2) + 470 ohm data resistor + MODE button + BRIGHTNESS button. The `BTN_LONG_PRESS_MS` threshold is overridden to 5000ms in the wokwi build because the simulation runs slower than real time.

## Usage

1. Power on the ESP32 via USB or battery
2. Play music near the microphone
3. Short press the mode button (GPIO 4) to cycle through patterns
4. Press the brightness button (GPIO 5) to step brightness up, wrapping back to dim at the top
5. Long press the mode button to switch between Audio Reactive and Manual modes

### Patterns

| Pattern | Description |
|---------|-------------|
| PulseBeat | Full strip pulses on each beat, color shifts with hits |
| RainbowWave | Hue rotates proportional to mid-frequency energy |
| StrobeKick | White flash on strong beats, colored glow between |
| FrequencyBars | Strip divided into 4 segments showing bass/lowMid/mid/high |
| SKZColors | Cycles through Stray Kids colors (neon green, red, black, white) on beat |

## Project Status

- [x] Phase 1: Audio-reactive prototype (firmware complete, awaiting hardware)
- [ ] Phase 2: Web interface and polish (WiFi AP, phone control, smooth transitions)
- [ ] Phase 3: BLE concert sync research (passive scanning, protocol analysis)
- [ ] Phase 4: Wearable build (soldering, enclosure, form factor)

## License

Personal project. Not affiliated with JYP Entertainment or Stray Kids.
