#include "settings.h"
#include "../config.h"
#include "../led/led_controller.h"
#include "../modes/mode_manager.h"
#include <Arduino.h>
#include <Preferences.h>

// Change detection is done by COMPARING the live state against what was last
// written, not by having every mutation call a mark_dirty() helper. Comparison
// cannot be forgotten: the web command drain, the button handler and any future
// control path are all covered without touching this file.

static Preferences prefs;
static bool prefs_ready = false;

// What is currently on flash. Seeded at init so a boot that changes nothing
// writes nothing.
static uint8_t saved_mode = 0;
static uint8_t saved_pattern = 0;
static uint8_t saved_brightness = 0;

// 0 means "live state matches flash". Otherwise it is the timestamp of the first
// observed difference, restarted on every further change.
static unsigned long dirty_since = 0;

static uint8_t live_mode() {
    return (uint8_t)mode_manager_get_mode();
}

void settings_init() {
    // read_only = false: the namespace has to be creatable on first boot.
    prefs_ready = prefs.begin(SETTINGS_NAMESPACE, false);
    if (!prefs_ready) {
        Serial.println("[SET]  NVS open failed, settings will not persist");
        // Still seed the shadow copies from the live state, so settings_service()
        // does not spin trying to write to a namespace that never opened.
        saved_mode = live_mode();
        saved_pattern = mode_manager_get_pattern_index();
        saved_brightness = led_get_brightness();
        return;
    }

    // Absent keys return the default, so a first boot reads the values that
    // mode_manager_init() and led_controller_init() already established.
    uint8_t stored_mode = prefs.getUChar("mode", (uint8_t)Mode::AUDIO_REACTIVE);
    uint8_t stored_pattern = prefs.getUChar("pattern", 0);
    uint8_t stored_brightness = prefs.getUChar("bright", MAX_BRIGHTNESS);

    // Validate before applying. A pattern index left over from a build with more
    // patterns would otherwise index patterns[] out of bounds, and a bad mode
    // would be cast into an enum with no matching enumerator.
    if (!mode_manager_is_valid_mode(stored_mode)) {
        Serial.printf("[SET]  stored mode %u invalid, using AUDIO_REACTIVE\n", stored_mode);
        stored_mode = (uint8_t)Mode::AUDIO_REACTIVE;
    }
    if (stored_pattern >= mode_manager_pattern_count()) {
        Serial.printf("[SET]  stored pattern %u out of range, using 0\n", stored_pattern);
        stored_pattern = 0;
    }

    mode_manager_set_mode((Mode)stored_mode);
    mode_manager_set_pattern(stored_pattern);
    led_set_brightness(stored_brightness);

    // Read brightness back rather than trusting the request: led_set_brightness()
    // clamps at BRIGHTNESS_LIMIT, and the shadow copy must match what is actually
    // in effect or settings_service() would see a permanent difference and write
    // on every debounce window.
    saved_mode = stored_mode;
    saved_pattern = stored_pattern;
    saved_brightness = led_get_brightness();

    Serial.printf("[SET]  restored mode=%u pattern=%u (%s) brightness=%u\n",
        saved_mode, saved_pattern, mode_manager_get_pattern_name(), saved_brightness);
}

void settings_service(unsigned long now) {
    if (!prefs_ready) return;

    uint8_t m = live_mode();
    uint8_t p = mode_manager_get_pattern_index();
    uint8_t b = led_get_brightness();

    bool differs = (m != saved_mode) || (p != saved_pattern) || (b != saved_brightness);

    if (!differs) {
        dirty_since = 0;
        return;
    }

    // Restart the window on every change, so a slider drag collapses into one
    // write once the user stops moving it.
    static uint8_t last_seen_m = 0, last_seen_p = 0, last_seen_b = 0;
    if (dirty_since == 0 || m != last_seen_m || p != last_seen_p || b != last_seen_b) {
        dirty_since = now;
        last_seen_m = m;
        last_seen_p = p;
        last_seen_b = b;
        // now == 0 is possible only in the first millisecond after boot; treat it
        // as 1 so the sentinel keeps its meaning.
        if (dirty_since == 0) dirty_since = 1;
        return;
    }

    if ((now - dirty_since) < SETTINGS_SAVE_DEBOUNCE_MS) return;

    // Write only what actually changed. Each putUChar commits immediately.
    if (m != saved_mode)       { prefs.putUChar("mode", m);   saved_mode = m; }
    if (p != saved_pattern)    { prefs.putUChar("pattern", p); saved_pattern = p; }
    if (b != saved_brightness) { prefs.putUChar("bright", b);  saved_brightness = b; }
    dirty_since = 0;

    Serial.printf("[SET]  saved mode=%u pattern=%u brightness=%u\n", m, p, b);
}
