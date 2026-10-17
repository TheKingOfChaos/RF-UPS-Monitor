#pragma once

/**
 * @file web_app.h
 * @brief The configuration web page: routes, login, CSRF checks and HTML.
 *        Pure C++, host tested with a fake backend.
 *
 * Pages:
 *   GET  /          login form, or the status and settings page when logged in
 *   POST /login     user "admin" + password; sets the session cookie
 *   POST /logout
 *   POST /network   DHCP or static IP; saves and reboots
 *   POST /community SNMP community; saves, active immediately
 *   POST /hostname  hostname (sysName, DHCP, mDNS); saves, active immediately
 *   POST /reboot
 *   GET  /static/bootstrap-<version>.min.css  Bootstrap from flash, gzip, cached a year, no login
 * Every POST except /login needs a valid session cookie and the session's
 * CSRF token in the form. The cookie is HttpOnly and SameSite=Strict.
 */

#include <stddef.h>
#include <stdint.h>
#include "http_request.h"
#include "web_auth.h"

/**
 * @brief What the status part of the page shows
 */
struct WebStatus {
    const char* version;
    uint32_t uptimeSeconds;
    bool mainsKnown;
    bool mainsPresent;
    uint32_t mainsChanges;
    int32_t boardTempDeciC;
    bool linkUp;
    uint32_t linkMbps;
    char ip[16];
    char mac[18];
    bool dhcp;            ///< Configured mode
    char staticIP[16];    ///< Stored static configuration (form defaults)
    char subnetMask[16];
    char gateway[16];
    char community[32];
    char hostname[33];    ///< Valid hostname in use (DHCP, mDNS name.local, sysName)
};

/// Events the backend may log
enum class WebEvent : uint8_t { LoginFailed, LoginLocked, LoggedIn };

/**
 * @brief What the page needs from the device; the firmware implements it
 */
class WebBackend {
public:
    virtual ~WebBackend() {}
    virtual void status(WebStatus& out) = 0;
    /// true if password is the current web password
    virtual bool checkPassword(const char* password) = 0;
    /// Save the network settings (static values only used when dhcp is false); reboot follows
    virtual bool saveNetwork(bool dhcp, const char* ip, const char* mask, const char* gateway) = 0;
    /// Save and apply a new SNMP community
    virtual bool saveCommunity(const char* community) = 0;
    /// Save and apply a new hostname (already validated): sysName, DHCP and mDNS
    virtual bool saveHostname(const char* hostname) = 0;
    /// Reboot once the response has been sent
    virtual void requestReboot() = 0;
    /// Fill out with random bytes from a good source
    virtual void random(uint8_t* out, size_t len) = 0;
    virtual void log(uint32_t sourceIP, WebEvent event) = 0;
};

static const size_t WEB_BODY_MAX = 8192;
/// Longest Cookie header read; browsers send every cookie of the host, not only ours
static const size_t WEB_COOKIE_HEADER_MAX = 1024;
static const size_t WEB_RESPONSE_MAX = WEB_BODY_MAX + 768;

class WebApp {
public:
    /**
     * @brief Build the response to a parsed request (state Complete, TooLarge or Bad)
     * @return The full HTTP response; valid until the next call
     */
    const char* handle(const HttpRequest& req, uint32_t sourceIP, uint32_t nowMs, WebBackend& backend, size_t& len);

    /// Log every browser out, e.g. after the password changed
    void logoutAll() { _sessions.clear(); }

    /**
     * @brief Body that follows the response handle() returned, sent straight from flash
     *        (the stylesheet); nullptr for normal pages, whose body is in the response itself
     */
    const uint8_t* staticBody(size_t& len) const {
        len = _staticLen;
        return _staticBody;
    }

    size_t sessionCount() const { return _sessions.count(); }

private:
    const char* login(const HttpRequest& req, uint32_t sourceIP, uint32_t nowMs, WebBackend& backend, size_t& len);
    const char* network(const HttpRequest& req, size_t slot, WebBackend& backend, size_t& len);
    const char* community(const HttpRequest& req, size_t slot, WebBackend& backend, size_t& len);
    const char* hostname(const HttpRequest& req, size_t slot, WebBackend& backend, size_t& len);
    const char* mainPage(int code, const char* reason, size_t slot, WebBackend& backend, const char* message,
                         bool error, size_t& len);
    const char* loginPage(int code, const char* reason, const char* message, size_t& len);
    const char* notice(int code, const char* reason, const char* title, const char* text, size_t& len);
    const char* redirect(const char* extraHeaders, size_t& len);
    const char* respond(int code, const char* reason, const char* extraHeaders, size_t& len);
    const char* stylesheet(size_t& len);

    WebSessions _sessions;
    LoginThrottle _throttle;
    char _cookies[WEB_COOKIE_HEADER_MAX];
    char _body[WEB_BODY_MAX];
    char _response[WEB_RESPONSE_MAX];
    const uint8_t* _staticBody = nullptr;
    size_t _staticLen = 0;
};
