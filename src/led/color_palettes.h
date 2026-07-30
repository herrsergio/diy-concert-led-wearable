#pragma once

#include <FastLED.h>

// Stray Kids official colors: black, neon green (#39FF14), red
DEFINE_GRADIENT_PALETTE(skz_palette) {
    0,     0,   0,   0,      // Black
    85,   57, 255,  20,      // Neon green
    170, 255,   0,   0,      // Red
    255,   0,   0,   0       // Back to black
};

// Concert energy: warm whites and golds
DEFINE_GRADIENT_PALETTE(concert_energy_palette) {
    0,   255, 255, 255,      // White
    64,  255, 200,  50,      // Gold
    128, 255, 100,   0,      // Orange
    192, 255,  50, 100,      // Pink
    255, 255, 255, 255       // White
};

// Ocean wave: blues and cyans
DEFINE_GRADIENT_PALETTE(ocean_palette) {
    0,     0,   0, 255,      // Blue
    64,    0, 100, 255,      // Azure
    128,   0, 255, 255,      // Cyan
    192,   0, 100, 255,      // Azure
    255,   0,   0, 255       // Blue
};
