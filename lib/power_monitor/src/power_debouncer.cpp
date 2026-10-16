#include "power_debouncer.h"

void PowerDebouncer::begin(bool level, uint32_t nowMs) {
    _stable = level;
    _pending = false;
    _changes = 0;
    _lastChangeMs = nowMs;
}

bool PowerDebouncer::update(bool level, uint32_t nowMs) {
    if (level == _stable) {
        // Back to the accepted level: whatever was being timed was a glitch
        _pending = false;
        return false;
    }

    if (!_pending) {
        _pending = true;
        _pendingSinceMs = nowMs;
    }

    // Unsigned subtraction is wrap-around safe
    if (nowMs - _pendingSinceMs < _debounceMs) return false;

    _stable = level;
    _pending = false;
    _changes++;
    _lastChangeMs = nowMs;
    return true;
}
