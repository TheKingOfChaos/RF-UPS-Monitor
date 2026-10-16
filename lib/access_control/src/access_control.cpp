#include "access_control.h"

// ---- RateLimiter ----

RateLimiter::Bucket* RateLimiter::find(uint32_t source, uint32_t nowMs) {
    Bucket* victim = nullptr;
    for (Bucket& b : _buckets) {
        if (b.used && b.source == source) return &b;
        if (!b.used) {
            if (victim == nullptr || victim->used) victim = &b;
        } else if (victim == nullptr || (victim->used && nowMs - b.lastMs > nowMs - victim->lastMs)) {
            // Least recently seen source so far
            victim = &b;
        }
    }
    // New source: start with a full bucket
    victim->used = true;
    victim->source = source;
    victim->milliTokens = _burst * 1000;
    victim->lastMs = nowMs;
    return victim;
}

void RateLimiter::refill(uint32_t& milliTokens, uint32_t& lastMs, uint32_t rate, uint32_t burst, uint32_t nowMs) {
    // An older timestamp than the last one adds nothing
    int32_t elapsed = (int32_t)(nowMs - lastMs);
    if (elapsed <= 0) return;
    uint64_t tokens = (uint64_t)milliTokens + (uint64_t)elapsed * rate;
    uint64_t full = (uint64_t)burst * 1000;
    milliTokens = (uint32_t)(tokens > full ? full : tokens);
    lastMs = nowMs;
}

bool RateLimiter::allow(uint32_t source, uint32_t nowMs) {
    Bucket* b = find(source, nowMs);
    refill(b->milliTokens, b->lastMs, _rate, _burst, nowMs);

    bool useGlobal = _global.rate > 0;
    if (useGlobal) {
        if (!_global.started) {
            _global.started = true;
            _global.lastMs = nowMs;
        }
        refill(_global.milliTokens, _global.lastMs, _global.rate, _global.burst, nowMs);
    }

    // Take tokens only if both buckets have one, so a refused source costs the global bucket nothing
    if (b->milliTokens >= 1000 && (!useGlobal || _global.milliTokens >= 1000)) {
        b->milliTokens -= 1000;
        if (useGlobal) _global.milliTokens -= 1000;
        return true;
    }
    _refused++;
    return false;
}

// ---- AccessLog ----

const char* accessEventName(AccessEvent event) {
    switch (event) {
    case AccessEvent::BadCommunity: return "bad community";
    case AccessEvent::BadVersion:   return "bad version";
    case AccessEvent::Malformed:    return "malformed";
    case AccessEvent::RateLimited:  return "rate limited";
    case AccessEvent::SetRejected:  return "set rejected";
    }
    return "unknown";
}

bool AccessLog::record(uint32_t source, AccessEvent event, uint32_t nowMs) {
    _total++;
    if (_size > 0) {
        Entry& last = _entries[(_head + CAPACITY - 1) % CAPACITY];
        if (last.source == source && last.event == event) {
            last.count++;
            last.lastMs = nowMs;
            return false;
        }
    }
    _entries[_head] = {nowMs, nowMs, source, 1, event};
    _head = (_head + 1) % CAPACITY;
    if (_size < CAPACITY) _size++;
    return true;
}

const AccessLog::Entry& AccessLog::newest(uint8_t index) const {
    if (_size == 0) return _entries[0];
    if (index >= _size) index = _size - 1;
    return _entries[(_head + CAPACITY - 1 - index) % CAPACITY];
}

void formatIPv4(uint32_t address, char* buf) {
    char* p = buf;
    for (int shift = 24; shift >= 0; shift -= 8) {
        uint8_t octet = (uint8_t)(address >> shift);
        if (octet >= 100) *p++ = (char)('0' + octet / 100);
        if (octet >= 10) *p++ = (char)('0' + octet / 10 % 10);
        *p++ = (char)('0' + octet % 10);
        if (shift) *p++ = '.';
    }
    *p = '\0';
}
