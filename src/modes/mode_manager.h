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
