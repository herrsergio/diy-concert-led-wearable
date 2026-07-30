# Music LED Strip

A wearable, audio-reactive LED strip built on ESP32-S3 that pulses and changes colors with music. Designed to bring the K-pop concert lightstick experience to a DIY LED wearable — specifically targeting Stray Kids concerts.

## What It Does

- **Audio-reactive mode**: An INMP441 microphone captures ambient music, runs FFT analysis, and drives LED patterns synced to the beat
- **BLE concert sync** (planned): Passively sniffs venue BLE broadcasts to mirror official lightstick colors
- **Manual mode** (planned): Phone control via WiFi AP web interface

## Hardware

| Component | Part |
|-----------|------|
| MCU | ESP32-S3-DevKitC-1 |
| LEDs | WS2812B strip, 60 LEDs/m, 1m |
| Microphone | INMP441 I2S MEMS |
| Power (dev) | USB-C 5V |
| Power (wearable) | 18650 Li-Ion + MT3608 boost converter |

### Wiring

Open [`docs/wiring-diagram.html`](docs/wiring-diagram.html) in a browser for the full visual diagram with color-coded wires and component layout.

Quick reference:

| ESP32-S3 Pin | Connects To | Wire Color |
|---|---|---|
| GPIO 38 | WS2812B DIN (through 470 ohm resistor) | Green |
| 5V | WS2812B +5V | Red |
| GND | WS2812B GND, INMP441 GND + L/R, Buttons | Black |
| 3.3V | INMP441 VDD | Yellow |
| GPIO 16 | INMP441 SCK (BCLK) | Blue |
| GPIO 15 | INMP441 WS (LRCLK) | Blue |
| GPIO 17 | INMP441 SD (DOUT) | Blue |
| GPIO 4 | Mode button -> GND | Orange |
| GPIO 5 | Brightness button -> GND | Orange |

Place a 1000uF capacitor across the strip's +5V and GND pins.

## Building and Flashing

Requires [PlatformIO](https://platformio.org/).

```bash
# Install PlatformIO CLI (if not already installed)
pip install platformio

# Compile
pio run

# Flash and open serial monitor
pio run --target upload --target monitor
```

## Usage

1. Power on the ESP32-S3 via USB or battery
2. Play music near the microphone
3. Short press the mode button (GPIO 4) to cycle through patterns
4. Press the brightness button (GPIO 5) to adjust brightness
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
