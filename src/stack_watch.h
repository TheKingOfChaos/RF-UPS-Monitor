#pragma once

/**
 * @file stack_watch.h
 * @brief High-water mark of core 0's stack.
 *
 * Core 0 has an 8 KB stack (core1_separate_stack), shared with USB, lwIP and W5500 interrupts.
 * At boot the unused part is filled with a pattern; the status screen then
 * shows how many bytes were never touched, so the HIL tests can check the
 * headroom after SNMP and web traffic.
 */

#include <stddef.h>

/// Paint the unused stack below the caller. Call once, early in setup() on core 0.
void stackWatchBegin();

/// Bytes of core 0 stack never used since boot (0 if stackWatchBegin() was not called)
size_t stackWatchNeverUsed();
