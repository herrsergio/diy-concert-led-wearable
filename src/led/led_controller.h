#pragma once

#include <FastLED.h>
#include "../config.h"

extern CRGB leds[NUM_LEDS];

void led_controller_init();
void led_show();
void led_clear();
void led_set_brightness(uint8_t brightness);
uint8_t led_get_brightness();
