#pragma once

#include <cstdint>
#include <cstddef>

void audio_capture_init();
size_t audio_capture_read(int32_t* buffer, size_t num_samples);
