#include "stack_watch.h"

#include <Arduino.h>

static const uint8_t PAINT = 0xA5;
// Stay clear of setup()'s own frame and an interrupt arriving while painting
static const size_t KEEP_CLEAR = 256;

static uint8_t* stackBottom = nullptr;
static uint8_t* paintedTop = nullptr;

void stackWatchBegin() {
    uint8_t* sp = (uint8_t*)__builtin_frame_address(0);
    int free = rp2040.getFreeStack();
    if (free <= (int)KEEP_CLEAR) return;
    stackBottom = sp - free;
    paintedTop = sp - KEEP_CLEAR;
    for (volatile uint8_t* p = stackBottom; p < paintedTop; p++) *p = PAINT;
}

size_t stackWatchNeverUsed() {
    if (stackBottom == nullptr) return 0;
    const volatile uint8_t* p = stackBottom;
    while (p < paintedTop && *p == PAINT) p++;
    return (size_t)(p - stackBottom);
}
