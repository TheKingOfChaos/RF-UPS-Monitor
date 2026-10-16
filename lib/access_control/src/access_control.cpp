#include "access_control.h"

// ---- RateLimiter ----

/**
 * @brief Eviction priority: free slot first, then untrusted, then trusted; least recently seen within each
 */
static int evictionRank(bool used, bool trusted) {
    return !used ? 0 : (trusted ? 2 : 1);
}

RateLimiter::Bucket* RateLimiter::find(uint32_t source, uint32_t nowMs) {
    Bucket* victim = nullptr;
    for (Bucket& b : _buckets) {
        if (b.used && b.source == source) return &b;
        if (victim == nullptr) {
            victim = &b;
            continue;
        }
        int rb = evictionRank(b.used, b.trusted), rv = evictionRank(victim->used, victim->trusted);
        if (rb < rv || (rb == rv && b.used && nowMs - b.lastMs > nowMs - victim->lastMs)) victim = &b;
    }
    // New source: start with a full bucket (the global bucket limits new sources as a group)
    victim->used = true;
    victim->trusted = false;
    victim->source = source;
    victim->milliTokens = _burst * 1000;
    victim->lastMs = nowMs;
    return victim;
}

void RateLimiter::configureTrusted(uint32_t globalRatePerSecond, uint32_t globalBurst, uint8_t maxTrusted,
                                   uint32_t trustTimeoutMs) {
    _trustedGlobal = {globalRatePerSecond, globalBurst, globalBurst * 1000u, 0, false};
    _maxTrusted = maxTrusted > MAX_SOURCES ? MAX_SOURCES : maxTrusted;
    _trustTimeoutMs = trustTimeoutMs;
}

void RateLimiter::markTrusted(uint32_t source) {
    for (Bucket& b : _buckets) {
        if (b.used && b.source == source && !b.trusted && trustedCount() < _maxTrusted) b.trusted = true;
    }
}

void RateLimiter::clearTrust() {
    for (Bucket& b : _buckets) b.trusted = false;
}

uint8_t RateLimiter::trustedCount() const {
    uint8_t n = 0;
    for (const Bucket& b : _buckets) {
        if (b.used && b.trusted) n++;
    }
    return n;
}

void RateLimiter::refill(uint32_t& milliTokens, uint32_t& lastMs, uint32_t rate, uint32_t burst, uint32_t nowMs) {
    // Unsigned: millis() only moves forward, and a gap of any length (even
    // more than 2^31 ms) must refill rather than look negative and freeze
    uint32_t elapsed = nowMs - lastMs;
    lastMs = nowMs;
    uint64_t full = (uint64_t)burst * 1000;
    uint64_t tokens = (uint64_t)milliTokens + (uint64_t)elapsed * rate;
    milliTokens = (uint32_t)(tokens > full ? full : tokens);
}

void RateLimiter::refillShared(SharedBucket& b, uint32_t nowMs) {
    if (!b.started) {
        b.started = true;
        b.lastMs = nowMs;
    }
    refill(b.milliTokens, b.lastMs, b.rate, b.burst, nowMs);
}

bool RateLimiter::allow(uint32_t source, uint32_t nowMs) {
    Bucket* b = find(source, nowMs);

    // Trust ends after a period without requests
    if (b->trusted && _trustTimeoutMs > 0 && nowMs - b->lastMs > _trustTimeoutMs) b->trusted = false;

    refill(b->milliTokens, b->lastMs, _rate, _burst, nowMs);

    // Trusted and untrusted sources each share their own bucket
    SharedBucket& shared = b->trusted ? _trustedGlobal : _global;
    bool useShared = shared.rate > 0;
    if (useShared) refillShared(shared, nowMs);

    // Take tokens only if both buckets have one, so a refused source costs the shared bucket nothing
    if (b->milliTokens >= 1000 && (!useShared || shared.milliTokens >= 1000)) {
        b->milliTokens -= 1000;
        if (useShared) shared.milliTokens -= 1000;
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
