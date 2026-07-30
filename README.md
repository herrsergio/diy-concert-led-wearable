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

```
ESP32-S3 GPIO 38  --[470Ω]--  WS2812B DIN
ESP32-S3 5V       ----------  WS2812B +5V  --┐
ESP32-S3 GND      ----------  WS2812B GND  --┤-- 1000uF cap
                                              │
ESP32-S3 GPIO 16  ----------  INMP441 SCK
ESP32-S3 GPIO 15  ----------  INMP441 WS
ESP32-S3 GPIO 17  ----------  INMP441 SD
ESP32-S3 3.3V     ----------  INMP441 VDD
ESP32-S3 GND      ----------  INMP441 GND + L/R

ESP32-S3 GPIO 4   -- Button -- GND  (pattern cycle)
ESP32-S3 GPIO 5   -- Button -- GND  (brightness)
```

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
