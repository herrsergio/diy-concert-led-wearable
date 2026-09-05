#pragma once

#include "../led/patterns.h"

enum class Mode {
    AUDIO_REACTIVE,
    BLE_SYNC,
    MANUAL
};

void mode_manager_init();
void mode_manager_next_pattern();
Mode mode_manager_get_mode();
void mode_manager_set_mode(Mode mode);
uint8_t mode_manager_get_pattern_index();
const char* mode_manager_get_pattern_name();
Pattern* mode_manager_get_current_pattern();

// Direct pattern selection, for control paths that can name a pattern instead of
// cycling to it (the web UI). Out-of-range indices are ignored rather than
// wrapped, so a stale value from NVS or a malformed request cannot index
// patterns[] out of bounds.
void mode_manager_set_pattern(uint8_t index);

// Enumeration, so a UI can list every pattern without cycling through them.
// Returns nullptr if index is out of range.
const char* mode_manager_get_pattern_name_at(uint8_t index);
uint8_t mode_manager_pattern_count();

// True if `value` is a valid Mode enumerator. Used to validate values arriving
// from NVS or from an HTTP request before casting them.
bool mode_manager_is_valid_mode(uint8_t value);
