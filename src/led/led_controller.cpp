#include "led_controller.h"

CRGB leds[NUM_LEDS];

void led_controller_init() {
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
        .setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(MAX_BRIGHTNESS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 1500);
    led_clear();
    led_show();
}

void led_show() {
    FastLED.show();
}

void led_clear() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
}

void led_set_brightness(uint8_t brightness) {
    FastLED.setBrightness(brightness);
}

uint8_t led_get_brightness() {
    return FastLED.getBrightness();
}
