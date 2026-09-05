#include "web_server.h"
#include "web_page.h"
#include "../config.h"
#include "../led/led_controller.h"
#include "../modes/mode_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// --- Cross-core plumbing ---------------------------------------------------
//
// Writes: the HTTP handlers (core 0) only enqueue. loop() (core 1) dequeues and
// applies. This keeps the existing single-threaded mutation model intact, so no
// lock is needed on the write path, and it guarantees every FastLED call still
// happens on core 1. A spinlock would have to be taken by readers too and
// disables interrupts; a mutex could block core 1, which is the one thing that
// must never happen.
//
// Reads: the handlers need current values to answer GET /api/state. They read the
// atomics below, published by core 1 after each drain, rather than calling the
// mode_manager getters across cores. That avoids relying on any assumption about
// which plain loads happen to be atomic on the Xtensa LX6.

struct WebCommand {
    enum Type : uint8_t { SET_MODE, SET_PATTERN, SET_BRIGHTNESS } type;
    uint8_t value;
};

static QueueHandle_t cmd_queue = NULL;
static WebServer server(WEB_SERVER_PORT);

static std::atomic<uint8_t> snap_mode{0};
static std::atomic<uint8_t> snap_pattern{0};
static std::atomic<uint8_t> snap_brightness{0};

static void publish_snapshot() {
    snap_mode.store((uint8_t)mode_manager_get_mode(), std::memory_order_relaxed);
    snap_pattern.store(mode_manager_get_pattern_index(), std::memory_order_relaxed);
    snap_brightness.store(led_get_brightness(), std::memory_order_relaxed);
}

// Returns false if the queue is full, which only happens if core 1 has stalled.
// Better to answer 503 than to block the socket and hold up the web task.
static bool enqueue(WebCommand::Type type, uint8_t value) {
    if (!cmd_queue) return false;
    WebCommand cmd = { type, value };
    return xQueueSend(cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

// --- JSON ------------------------------------------------------------------
//
// Hand-built rather than via ArduinoJson. The payload is three integers plus a
// fixed array of compile-time pattern names we control, so there is nothing to
// escape and no untrusted input to serialize. The input side needs no parser
// either, because the values arrive as query arguments. ArduinoJson stays
// commented out in platformio.ini for the BLE phase.

static void build_state_json(char* out, size_t len) {
    int n = snprintf(out, len,
        "{\"mode\":%u,\"pattern\":%u,\"brightness\":%u,\"patterns\":[",
        (unsigned)snap_mode.load(std::memory_order_relaxed),
        (unsigned)snap_pattern.load(std::memory_order_relaxed),
        (unsigned)snap_brightness.load(std::memory_order_relaxed));
    if (n < 0 || (size_t)n >= len) { snprintf(out, len, "{}"); return; }

    uint8_t count = mode_manager_pattern_count();
    for (uint8_t i = 0; i < count; i++) {
        const char* name = mode_manager_get_pattern_name_at(i);
        if (!name) break;
        int w = snprintf(out + n, len - n, "%s\"%s\"", i ? "," : "", name);
        if (w < 0 || (size_t)(n + w) >= len) { snprintf(out, len, "{}"); return; }
        n += w;
    }
    snprintf(out + n, len - n, "]}");
}

static void send_state() {
    char json[384];
    build_state_json(json, sizeof(json));
    server.send(200, "application/json", json);
}

// --- Handlers (all run on core 0) -----------------------------------------

static void handle_root() {
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handle_get_state() {
    send_state();
}

// POST /api/set?mode=&pattern=&brightness= -- any subset. One queued command per
// argument present. Values are range-checked here so a malformed request is
// rejected with 400 instead of travelling further into the firmware; the setters
// on core 1 validate again as a second line of defence.
static void handle_set() {
    bool any = false;

    if (server.hasArg("mode")) {
        long v = server.arg("mode").toInt();
        if (v < 0 || v > 255 || !mode_manager_is_valid_mode((uint8_t)v)) {
            server.send(400, "text/plain", "invalid mode");
            return;
        }
        if (!enqueue(WebCommand::SET_MODE, (uint8_t)v)) {
            server.send(503, "text/plain", "busy");
            return;
        }
        any = true;
    }

    if (server.hasArg("pattern")) {
        long v = server.arg("pattern").toInt();
        if (v < 0 || v >= (long)mode_manager_pattern_count()) {
            server.send(400, "text/plain", "invalid pattern");
            return;
        }
        if (!enqueue(WebCommand::SET_PATTERN, (uint8_t)v)) {
            server.send(503, "text/plain", "busy");
            return;
        }
        any = true;
    }

    if (server.hasArg("brightness")) {
        long v = server.arg("brightness").toInt();
        if (v < 0 || v > 255) {
            server.send(400, "text/plain", "invalid brightness");
            return;
        }
        if (!enqueue(WebCommand::SET_BRIGHTNESS, (uint8_t)v)) {
            server.send(503, "text/plain", "busy");
            return;
        }
        any = true;
    }

    if (!any) {
        server.send(400, "text/plain", "no recognized argument");
        return;
    }

    // The commands are queued, not yet applied, so the snapshot is one drain
    // behind. Wait briefly for core 1 to catch up so the phone renders the value
    // it just set instead of the previous one. Bounded, and this is core 0, so a
    // slow answer here costs the audio path nothing.
    for (int i = 0; i < 20 && uxQueueMessagesWaiting(cmd_queue) > 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    send_state();
}

static void handle_not_found() {
    server.send(404, "text/plain", "not found");
}

// --- Core 0 task ----------------------------------------------------------

static void web_task(void* arg) {
    (void)arg;
    for (;;) {
        server.handleClient();
        // Yield so the idle task and the WiFi stack on this core get to run.
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// --- Public API ----------------------------------------------------------

void web_server_init() {
    publish_snapshot();

    cmd_queue = xQueueCreate(WEB_CMD_QUEUE_LEN, sizeof(WebCommand));
    if (!cmd_queue) {
        Serial.println("[WEB] queue alloc failed, web control disabled");
        return;
    }

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(WIFI_SSID, WIFI_PASSWORD)) {
        Serial.println("[WEB] softAP failed, web control disabled");
        return;
    }

    // Read the address back from the driver rather than assuming a default.
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WEB] AP \"%s\" up at http://%s\n", WIFI_SSID, ip.toString().c_str());

    if (MDNS.begin(WIFI_HOSTNAME)) {
        MDNS.addService("http", "tcp", WEB_SERVER_PORT);
        Serial.printf("[WEB] also http://%s.local (mDNS support varies by phone)\n",
                      WIFI_HOSTNAME);
    } else {
        Serial.println("[WEB] mDNS failed; use the numeric address above");
    }

    server.on("/", HTTP_GET, handle_root);
    server.on("/api/state", HTTP_GET, handle_get_state);
    server.on("/api/set", HTTP_POST, handle_set);
    server.onNotFound(handle_not_found);
    server.begin();

    BaseType_t ok = xTaskCreatePinnedToCore(
        web_task, "web", WEB_TASK_STACK, NULL, 1, NULL, WEB_TASK_CORE);
    if (ok != pdPASS) {
        Serial.println("[WEB] task create failed, web control disabled");
        return;
    }
    Serial.printf("[WEB] server task on core %d\n", WEB_TASK_CORE);
}

void web_server_service(unsigned long now) {
    (void)now;
    if (!cmd_queue) return;

    WebCommand cmd;
    while (xQueueReceive(cmd_queue, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
        case WebCommand::SET_MODE:
            if (mode_manager_is_valid_mode(cmd.value)) {
                mode_manager_set_mode((Mode)cmd.value);
                Serial.printf("[WEB] mode=%u\n", cmd.value);
            }
            break;
        case WebCommand::SET_PATTERN:
            mode_manager_set_pattern(cmd.value);
            Serial.printf("[WEB] pattern=%u: %s\n",
                          mode_manager_get_pattern_index(),
                          mode_manager_get_pattern_name());
            break;
        case WebCommand::SET_BRIGHTNESS:
            led_set_brightness(cmd.value);
            Serial.printf("[WEB] brightness=%u\n", led_get_brightness());
            break;
        }
    }

    publish_snapshot();
}
