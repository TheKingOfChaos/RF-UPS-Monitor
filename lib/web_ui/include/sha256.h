#pragma once

/**
 * @file sha256.h
 * @brief SHA-256 (FIPS 180-4), for hashing the web password. Pure C++, host tested.
 */

#include <stddef.h>
#include <stdint.h>

static const size_t SHA256_DIGEST_LEN = 32;

/**
 * @brief Incremental SHA-256
 */
class Sha256 {
public:
    Sha256();

    /// Add data; may be called any number of times before finish()
    void update(const void* data, size_t len);

    /// Write the 32-byte digest; the object must not be used afterwards
    void finish(uint8_t digest[SHA256_DIGEST_LEN]);

private:
    void block(const uint8_t* p);

    uint32_t _h[8];
    uint8_t _buf[64];
    size_t _bufLen;
    uint64_t _total;
};

/// One-shot SHA-256 of a buffer
void sha256(const void* data, size_t len, uint8_t digest[SHA256_DIGEST_LEN]);
