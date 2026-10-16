// Host unit tests for lib/access_control: per-source rate limiting and the access log.
// Run: pio test -e native -f native/test_access_control

#include <unity.h>
#include <string.h>
#include <access_control.h>

void setUp() {}
void tearDown() {}

static const uint32_t PC = 0x0A0A0641;     // 10.10.6.65
static const uint32_t OTHER = 0x0A0A0642;  // 10.10.6.66

// ---- RateLimiter ----

static void test_burst_is_allowed_then_refused() {
    RateLimiter r(10, 5);
    for (int i = 0; i < 5; i++) TEST_ASSERT_TRUE_MESSAGE(r.allow(PC, 1000), "within burst");
    TEST_ASSERT_FALSE(r.allow(PC, 1000));
    TEST_ASSERT_EQUAL_UINT32(1, r.refused());
}

static void test_tokens_refill_at_the_rate() {
    RateLimiter r(10, 5);                       // one token per 100 ms
    for (int i = 0; i < 5; i++) r.allow(PC, 0);
    TEST_ASSERT_FALSE(r.allow(PC, 99));
    TEST_ASSERT_TRUE(r.allow(PC, 100));
    TEST_ASSERT_FALSE(r.allow(PC, 150));
    TEST_ASSERT_TRUE(r.allow(PC, 200));
}

static void test_refill_never_exceeds_burst() {
    RateLimiter r(10, 5);
    r.allow(PC, 0);
    // An hour of silence still only gives `burst` tokens
    for (int i = 0; i < 5; i++) TEST_ASSERT_TRUE(r.allow(PC, 3600000));
    TEST_ASSERT_FALSE(r.allow(PC, 3600000));
}

static void test_sustained_rate_over_ten_seconds() {
    RateLimiter r(50, 100);
    uint32_t allowed = 0;
    for (uint32_t t = 0; t < 10000; t++) {      // 1000 requests per second offered
        if (r.allow(PC, t)) allowed++;
    }
    // burst + 10 s * 50/s
    TEST_ASSERT_UINT32_WITHIN(2, 100 + 500, allowed);
}

static void test_sources_are_limited_independently() {
    RateLimiter r(10, 2);
    TEST_ASSERT_TRUE(r.allow(PC, 0));
    TEST_ASSERT_TRUE(r.allow(PC, 0));
    TEST_ASSERT_FALSE(r.allow(PC, 0));
    TEST_ASSERT_TRUE(r.allow(OTHER, 0));        // a flooding PC does not lock out others
    TEST_ASSERT_TRUE(r.allow(OTHER, 0));
}

static void test_full_table_replaces_least_recently_seen_source() {
    RateLimiter r(1, 1);
    for (uint32_t i = 0; i < RateLimiter::MAX_SOURCES; i++) TEST_ASSERT_TRUE(r.allow(100 + i, i));
    TEST_ASSERT_FALSE(r.allow(100, 20));        // source 100 is out of tokens
    // A new source evicts the least recently seen one (101), not the active 100
    TEST_ASSERT_TRUE(r.allow(999, 30));
    TEST_ASSERT_FALSE(r.allow(100, 31));        // still tracked, still limited
}

static void test_rate_limiter_survives_millis_wraparound() {
    RateLimiter r(10, 1);
    TEST_ASSERT_TRUE(r.allow(PC, 0xFFFFFFFFu - 50));
    TEST_ASSERT_FALSE(r.allow(PC, 0xFFFFFFFFu));
    TEST_ASSERT_TRUE(r.allow(PC, 49));          // 100 ms later, across the wrap
}

static void test_time_going_backwards_does_not_add_tokens() {
    RateLimiter r(10, 1);
    TEST_ASSERT_TRUE(r.allow(PC, 1000));
    // A sample older than the last one (e.g. from another core) must not refill
    TEST_ASSERT_FALSE(r.allow(PC, 999));
}

// ---- global limit (cannot be reset by rotating or spoofed sources) ----

static void test_rotating_sources_cannot_exceed_global_limit() {
    // Every request from a new address: each one would get a fresh per-source bucket
    RateLimiter r(50, 100, 100, 200);
    uint32_t allowed = 0;
    for (uint32_t t = 0; t < 1000; t++) {
        if (r.allow(0x0A000000u + t, t)) allowed++;
    }
    // Global burst 200 + 1 s * 100/s
    TEST_ASSERT_UINT32_WITHIN(2, 300, allowed);
}

static void test_refused_source_does_not_drain_global_bucket() {
    RateLimiter r(10, 2, 100, 3);
    TEST_ASSERT_TRUE(r.allow(PC, 0));
    TEST_ASSERT_TRUE(r.allow(PC, 0));
    for (int i = 0; i < 50; i++) TEST_ASSERT_FALSE(r.allow(PC, 0));   // PC flooding, refused per source
    TEST_ASSERT_TRUE(r.allow(OTHER, 0));                             // global still has its third token
}

static void test_single_source_is_limited_by_its_own_bucket_under_global() {
    RateLimiter r(50, 100, 100, 200);
    uint32_t allowed = 0;
    for (uint32_t t = 0; t < 10000; t++) {
        if (r.allow(PC, t)) allowed++;
    }
    TEST_ASSERT_UINT32_WITHIN(2, 100 + 500, allowed);
}

static void test_global_limit_is_off_by_default() {
    RateLimiter r(10, 1);
    for (uint32_t i = 0; i < 1000; i++) TEST_ASSERT_TRUE(r.allow(i, 0));   // all new sources
}

// ---- AccessLog ----

static void test_log_records_newest_first() {
    AccessLog log;
    TEST_ASSERT_TRUE(log.record(PC, AccessEvent::BadCommunity, 10));
    TEST_ASSERT_TRUE(log.record(OTHER, AccessEvent::BadVersion, 20));
    TEST_ASSERT_EQUAL_UINT8(2, log.size());
    TEST_ASSERT_EQUAL_UINT32(OTHER, log.newest(0).source);
    TEST_ASSERT_EQUAL(AccessEvent::BadVersion, log.newest(0).event);
    TEST_ASSERT_EQUAL_UINT32(PC, log.newest(1).source);
}

static void test_repeats_are_folded_into_one_entry() {
    AccessLog log;
    TEST_ASSERT_TRUE(log.record(PC, AccessEvent::BadCommunity, 10));
    TEST_ASSERT_FALSE(log.record(PC, AccessEvent::BadCommunity, 20));
    TEST_ASSERT_FALSE(log.record(PC, AccessEvent::BadCommunity, 30));
    TEST_ASSERT_EQUAL_UINT8(1, log.size());
    TEST_ASSERT_EQUAL_UINT32(3, log.newest(0).count);
    TEST_ASSERT_EQUAL_UINT32(10, log.newest(0).firstMs);
    TEST_ASSERT_EQUAL_UINT32(30, log.newest(0).lastMs);
    TEST_ASSERT_EQUAL_UINT32(3, log.total());
}

static void test_different_event_or_source_starts_new_entry() {
    AccessLog log;
    log.record(PC, AccessEvent::BadCommunity, 1);
    TEST_ASSERT_TRUE(log.record(PC, AccessEvent::RateLimited, 2));
    TEST_ASSERT_TRUE(log.record(OTHER, AccessEvent::RateLimited, 3));
    TEST_ASSERT_TRUE(log.record(PC, AccessEvent::BadCommunity, 4));  // not adjacent to the first
    TEST_ASSERT_EQUAL_UINT8(4, log.size());
}

static void test_log_keeps_only_the_most_recent_entries() {
    AccessLog log;
    for (uint32_t i = 0; i < AccessLog::CAPACITY + 5; i++) log.record(i, AccessEvent::Malformed, i);
    TEST_ASSERT_EQUAL_UINT8(AccessLog::CAPACITY, log.size());
    TEST_ASSERT_EQUAL_UINT32(AccessLog::CAPACITY + 4, log.newest(0).source);
    TEST_ASSERT_EQUAL_UINT32(5, log.newest(AccessLog::CAPACITY - 1).source);
    TEST_ASSERT_EQUAL_UINT32(AccessLog::CAPACITY + 5, log.total());
}

static void test_newest_out_of_range_is_clamped() {
    AccessLog log;
    log.record(PC, AccessEvent::Malformed, 1);
    TEST_ASSERT_EQUAL_UINT32(PC, log.newest(200).source);
}

static void test_event_names_and_ip_format() {
    TEST_ASSERT_EQUAL_STRING("bad community", accessEventName(AccessEvent::BadCommunity));
    TEST_ASSERT_EQUAL_STRING("bad version", accessEventName(AccessEvent::BadVersion));
    TEST_ASSERT_EQUAL_STRING("malformed", accessEventName(AccessEvent::Malformed));
    TEST_ASSERT_EQUAL_STRING("rate limited", accessEventName(AccessEvent::RateLimited));
    TEST_ASSERT_EQUAL_STRING("set rejected", accessEventName(AccessEvent::SetRejected));
    char buf[16];
    formatIPv4(PC, buf);
    TEST_ASSERT_EQUAL_STRING("10.10.6.65", buf);
    formatIPv4(0xFFFFFFFFu, buf);
    TEST_ASSERT_EQUAL_STRING("255.255.255.255", buf);
    formatIPv4(0, buf);
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", buf);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_burst_is_allowed_then_refused);
    RUN_TEST(test_tokens_refill_at_the_rate);
    RUN_TEST(test_refill_never_exceeds_burst);
    RUN_TEST(test_sustained_rate_over_ten_seconds);
    RUN_TEST(test_sources_are_limited_independently);
    RUN_TEST(test_full_table_replaces_least_recently_seen_source);
    RUN_TEST(test_rate_limiter_survives_millis_wraparound);
    RUN_TEST(test_time_going_backwards_does_not_add_tokens);
    RUN_TEST(test_rotating_sources_cannot_exceed_global_limit);
    RUN_TEST(test_refused_source_does_not_drain_global_bucket);
    RUN_TEST(test_single_source_is_limited_by_its_own_bucket_under_global);
    RUN_TEST(test_global_limit_is_off_by_default);
    RUN_TEST(test_log_records_newest_first);
    RUN_TEST(test_repeats_are_folded_into_one_entry);
    RUN_TEST(test_different_event_or_source_starts_new_entry);
    RUN_TEST(test_log_keeps_only_the_most_recent_entries);
    RUN_TEST(test_newest_out_of_range_is_clamped);
    RUN_TEST(test_event_names_and_ip_format);
    return UNITY_END();
}
