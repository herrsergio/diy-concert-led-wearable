#pragma once

// Persistence of the three user-facing settings (mode, pattern, brightness) in
// NVS, so a power cycle does not lose them. This matters more than it looks:
// the buttons are not wired, so without persistence there is no way to restore
// a setting after a reboot except by connecting a phone again.
//
// This lives outside src/web/ on purpose. The button path mutates the same state
// and must persist too, so persistence is not a web concern.

void settings_init();

// Called from loop() on core 1. Compares the live state against the last value
// written and saves it once it has been stable for SETTINGS_SAVE_DEBOUNCE_MS.
void settings_service(unsigned long now);
