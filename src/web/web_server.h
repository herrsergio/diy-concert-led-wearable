#pragma once

// WiFi access point plus HTTP control interface for mode, pattern and brightness.
//
// THREADING CONTRACT, and the reason this module is split the way it is:
//
// The audio path has no timing margin. The INMP441 produces one 1024-sample
// window every 23.22 ms and the measured gap_avg already sits at 23.15-23.22 ms,
// so anything added to core 1 risks permanent DMA loss (see the [I2S] diagnostic
// in audio_capture.cpp). The Arduino loopTask runs on core 1
// (CONFIG_ARDUINO_RUNNING_CORE=1) and the lwIP task on core 0
// (CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0), so HTTP is served by its own task
// pinned to core 0, next to the network stack it talks to.
//
// Because it has its own task, the server does NOT need to be asynchronous:
// blocking inside handleClient() blocks only that task. That is why this uses the
// bundled synchronous WebServer instead of ESPAsyncWebServer, and why it needs no
// new library dependency at all.
//
// The core-0 task NEVER mutates shared state. It pushes a command onto a queue
// that loop() drains on core 1, so mode, pattern index, the LED buffer and every
// FastLED call stay exactly as single-threaded as they were before. For reads it
// sees only an atomic snapshot published by core 1.

void web_server_init();

// Called from loop() on core 1. Applies any commands queued by the web task and
// republishes the state snapshot the web task reads.
void web_server_service(unsigned long now);
