#include "mains_input.h"

#include <power_debouncer.h>
#include "pins.h"

// Core 1 private state
static PowerDebouncer debouncer(MAINS_DEBOUNCE_MS);
static uint32_t sampleCount = 0;

// Published snapshot, written only by core 1. `seq` is odd while a write is
// in progress and 0 until the first publish.
static volatile uint32_t seq = 0;
static volatile MainsSnapshot published;

/**
 * @brief Publish the current state (core 1, single writer)
 */
static void publish(bool raw) {
    seq = seq + 1;          // Odd: write in progress
    __dmb();
    published.present = debouncer.state();
    published.raw = raw;
    published.changes = debouncer.changes();
    published.lastChangeMs = debouncer.lastChangeMs();
    published.samples = sampleCount;
    __dmb();
    seq = seq + 1;          // Even: snapshot complete
}

void mainsBegin() {
    pinMode(POWER_PIN, INPUT_PULLDOWN);
    delayMicroseconds(50);  // Let the pull-down settle
    bool level = digitalRead(POWER_PIN) == HIGH;
    debouncer.begin(level, millis());
    sampleCount = 0;
    publish(level);
}

void mainsSample() {
    bool level = digitalRead(POWER_PIN) == HIGH;
    debouncer.update(level, millis());
    sampleCount++;
    publish(level);
}

bool mainsRead(MainsSnapshot& out) {
    for (;;) {
        uint32_t before = seq;
        if (before == 0) return false;
        __dmb();
        out.present = published.present;
        out.raw = published.raw;
        out.changes = published.changes;
        out.lastChangeMs = published.lastChangeMs;
        out.samples = published.samples;
        __dmb();
        // Retry if a write was in progress or happened while copying
        if ((before & 1u) == 0 && seq == before) return true;
    }
}
