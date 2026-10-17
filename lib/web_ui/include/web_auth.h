#pragma once

/**
 * @file web_auth.h
 * @brief Web login: password hashing, the device default password, sessions
 *        and the failed-login throttle. Pure C++, host tested.
 *
 * Passwords are stored as "<salt hex>$<hash hex>": a 16-byte random salt and
 * SHA-256 iterated WEB_HASH_ITERATIONS times over salt and password, so the
 * settings file never holds the password itself. Checks are constant time.
 */

#include <stddef.h>
#include <stdint.h>

static const size_t WEB_PASSWORD_MIN = 8;
static const size_t WEB_PASSWORD_MAX = 32;
static const size_t WEB_SALT_LEN = 16;
/// Stored form: 32 hex salt + '$' + 64 hex hash
static const size_t WEB_PASSWORD_HASH_LEN = 2 * WEB_SALT_LEN + 1 + 64;
static const uint32_t WEB_HASH_ITERATIONS = 2000;
/// Characters of the generated default password: no 0/o/1/l to avoid misreading
static const size_t WEB_DEFAULT_PASSWORD_LEN = 10;

/// The only user name
static const char WEB_USER[] = "admin";

/**
 * @brief A password the user may choose: 8-32 printable ASCII characters, no spaces
 */
bool webPasswordIsValid(const char* password);

/**
 * @brief Hash a password with the given salt into out ("<salt>$<hash>", NUL-terminated)
 * @param out Buffer of at least WEB_PASSWORD_HASH_LEN + 1 bytes
 */
void webHashPassword(const char* password, const uint8_t salt[WEB_SALT_LEN], char* out);

/**
 * @brief true if stored has the "<32 hex>$<64 hex>" form (lower-case hex)
 */
bool webPasswordHashIsWellFormed(const char* stored);

/**
 * @brief Check a password against a stored hash, in time independent of where they differ
 */
bool webCheckPassword(const char* password, const char* stored);

/**
 * @brief webCheckPassword() in small steps, so a login never blocks the main loop
 *
 * The full check takes WEB_HASH_ITERATIONS SHA-256 rounds (about 0.45 s on the
 * RP2040). The firmware runs a few rounds per loop pass and serves SNMP in
 * between. The result equals webCheckPassword(password, stored).
 */
class WebPasswordCheck {
public:
    WebPasswordCheck() { clear(); }

    /// Start checking password against stored (copies both)
    void begin(const char* password, const char* stored);

    /**
     * @brief Run up to maxRounds hash rounds
     * @return true once the check is finished (then matches() is valid)
     */
    bool step(uint32_t maxRounds);

    bool active() const { return _active; }
    bool done() const { return _done; }
    bool matches() const { return _done && _matches; }

    /// Forget everything, wiping the copied password
    void clear();

private:
    char _password[WEB_PASSWORD_MAX + 1];
    char _stored[WEB_PASSWORD_HASH_LEN + 1];
    uint8_t _salt[WEB_SALT_LEN];
    uint8_t _digest[32];
    uint32_t _round;
    bool _valid;  ///< stored is well formed and the password fits
    bool _active;
    bool _done;
    bool _matches;
};

/**
 * @brief Build the device default password from random bytes
 * @param random At least WEB_DEFAULT_PASSWORD_LEN bytes
 * @param out    Buffer of at least WEB_DEFAULT_PASSWORD_LEN + 1 bytes
 */
void webMakeDefaultPassword(const uint8_t* random, char* out);

/**
 * @brief Compare two NUL-terminated strings in time that depends only on their lengths
 */
bool webConstantTimeEquals(const char* a, const char* b);

static const size_t WEB_MAX_SESSIONS = 2;
static const uint32_t WEB_SESSION_IDLE_MS = 15UL * 60 * 1000;
static const size_t WEB_TOKEN_HEX = 32;   ///< 128-bit tokens as hex
static const size_t WEB_SESSION_RANDOM = 32;  ///< random bytes per session: token + CSRF token

/**
 * @brief Logged-in browser sessions, each with a session token (cookie) and a
 *        CSRF token (hidden form field). Idle sessions expire; when all slots
 *        are taken, a new login replaces the least recently used session.
 */
class WebSessions {
public:
    WebSessions();

    /**
     * @brief Start a session
     * @param random WEB_SESSION_RANDOM fresh random bytes
     * @return Slot index, used with token() and csrf()
     */
    size_t create(const uint8_t random[WEB_SESSION_RANDOM], uint32_t nowMs);

    /**
     * @brief Find the session for a cookie token and refresh its idle timer
     * @return Slot index, or -1 if unknown or expired
     */
    int find(const char* token, uint32_t nowMs);

    const char* token(size_t slot) const { return _s[slot].token; }
    const char* csrf(size_t slot) const { return _s[slot].csrf; }

    /// End one session (logout)
    void remove(size_t slot);

    /// End all sessions, e.g. after the password changed
    void clear();

    /// Sessions currently active (not counting expired ones not yet looked up)
    size_t count() const;

private:
    struct Session {
        bool used;
        uint32_t lastMs;
        char token[WEB_TOKEN_HEX + 1];
        char csrf[WEB_TOKEN_HEX + 1];
    };
    Session _s[WEB_MAX_SESSIONS];
};

static const uint8_t WEB_LOGIN_MAX_FAILURES = 5;
static const uint32_t WEB_LOGIN_LOCKOUT_MS = 30000;

/**
 * @brief After WEB_LOGIN_MAX_FAILURES failed logins in a row, refuse all logins
 *        for WEB_LOGIN_LOCKOUT_MS. Global, not per address, so changing the
 *        source address does not help an attacker.
 */
class LoginThrottle {
public:
    LoginThrottle() : _failures(0), _locked(false), _lockedAtMs(0) {}

    /// true if a login attempt may be checked now
    bool allowed(uint32_t nowMs);

    /// Seconds until logins are allowed again (0 if allowed)
    uint32_t secondsLeft(uint32_t nowMs);

    void failed(uint32_t nowMs);
    void succeeded() { _failures = 0; }

private:
    uint8_t _failures;
    bool _locked;
    uint32_t _lockedAtMs;
};
