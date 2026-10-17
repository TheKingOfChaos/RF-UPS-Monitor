#include "web_app.h"

#include <string.h>
#include <netconfig.h>
#include <settings_codec.h>

// Headers on every response. The page has no scripts at all; the policy keeps it that way.
static const char COMMON_HEADERS[] =
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "X-Frame-Options: DENY\r\n"
    "X-Content-Type-Options: nosniff\r\n"
    "Referrer-Policy: no-referrer\r\n"
    "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; "
    "frame-ancestors 'none'\r\n"
    "Connection: close\r\n";

static const char STYLE[] =
    "body{font-family:system-ui,sans-serif;max-width:40em;margin:2em auto;padding:0 1em;color:#222}"
    "h1{font-size:1.4em}table{border-collapse:collapse}td{padding:2px 1.5em 2px 0}"
    "fieldset{margin:1em 0;border:1px solid #bbb;border-radius:6px}"
    "label{display:block;margin:.4em 0}input[type=text],input[type=password]{width:14em}"
    ".msg{padding:.5em;background:#e6f4ea}.err{padding:.5em;background:#fde7e9}"
    "form.inline{display:inline}";

static void pageStart(TextBuffer& b, const char* title) {
    b.add("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
          "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
    b.addEscaped(title);
    b.add("</title><style>");
    b.add(STYLE);
    b.add("</style></head><body>");
}

static void pageEnd(TextBuffer& b) { b.add("</body></html>"); }

static void message(TextBuffer& b, const char* text, bool error) {
    if (text == nullptr || text[0] == '\0') return;
    b.add(error ? "<p class=\"err\">" : "<p class=\"msg\">");
    b.addEscaped(text);
    b.add("</p>");
}

static void csrfField(TextBuffer& b, const char* csrf) {
    b.add("<input type=\"hidden\" name=\"csrf\" value=\"");
    b.addEscaped(csrf);
    b.add("\">");
}

static void row(TextBuffer& b, const char* name, const char* value) {
    b.add("<tr><td>");
    b.add(name);
    b.add("</td><td>");
    b.addEscaped(value);
    b.add("</td></tr>");
}

static void textInput(TextBuffer& b, const char* label, const char* name, const char* value, size_t maxLen) {
    b.add("<label>");
    b.add(label);
    b.add("<br><input type=\"text\" name=\"");
    b.add(name);
    b.add("\" maxlength=\"");
    b.addUnsigned((uint32_t)maxLen);
    b.add("\" value=\"");
    b.addEscaped(value);
    b.add("\"></label>");
}

const char* WebApp::respond(int code, const char* reason, const char* extraHeaders, size_t& len) {
    TextBuffer out(_response, sizeof(_response));
    out.add("HTTP/1.1 ");
    out.addUnsigned((uint32_t)code);
    out.add(" ");
    out.add(reason);
    out.add("\r\n");
    out.add(COMMON_HEADERS);
    if (extraHeaders) out.add(extraHeaders);
    out.add("Content-Length: ");
    size_t bodyLen = strlen(_body);
    out.addUnsigned((uint32_t)bodyLen);
    out.add("\r\n\r\n");
    out.add(_body, bodyLen);
    if (!out.ok()) {
        // Cannot happen with the fixed pages; answer something valid anyway
        static const char FAIL[] = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        len = sizeof(FAIL) - 1;
        return FAIL;
    }
    len = out.length();
    return _response;
}

const char* WebApp::loginPage(int code, const char* reason, const char* text, size_t& len) {
    TextBuffer b(_body, sizeof(_body));
    pageStart(b, "RF-UPS-Monitor login");
    b.add("<h1>RF-UPS-Monitor</h1>");
    message(b, text, true);
    b.add("<form method=\"post\" action=\"/login\"><fieldset><legend>Log in</legend>"
          "<label>User<br><input type=\"text\" name=\"user\" value=\"admin\" maxlength=\"16\" "
          "autocomplete=\"username\"></label>"
          "<label>Password<br><input type=\"password\" name=\"password\" maxlength=\"32\" autofocus "
          "autocomplete=\"current-password\"></label>"
          "<button type=\"submit\">Log in</button></fieldset></form>"
          "<p>The default password is shown on the serial status screen (menu option 1) and can be "
          "changed with menu option 6.</p>");
    pageEnd(b);
    return respond(code, reason, nullptr, len);
}

const char* WebApp::notice(int code, const char* reason, const char* title, const char* text, size_t& len) {
    TextBuffer b(_body, sizeof(_body));
    pageStart(b, title);
    b.add("<h1>");
    b.addEscaped(title);
    b.add("</h1><p>");
    b.addEscaped(text);
    b.add("</p><p><a href=\"/\">Back to the start page</a></p>");
    pageEnd(b);
    return respond(code, reason, nullptr, len);
}

const char* WebApp::redirect(const char* extraHeaders, size_t& len) {
    _body[0] = '\0';  // a redirect has no body
    char headers[160];
    TextBuffer hb(headers, sizeof(headers));
    hb.add("Location: /\r\n");
    if (extraHeaders) hb.add(extraHeaders);
    return respond(303, "See Other", headers, len);
}

const char* WebApp::mainPage(int code, const char* reason, size_t slot, WebBackend& backend, const char* text,
                             bool error, size_t& len) {
    WebStatus st;
    memset(&st, 0, sizeof(st));
    backend.status(st);
    const char* csrf = _sessions.csrf(slot);

    TextBuffer b(_body, sizeof(_body));
    pageStart(b, "RF-UPS-Monitor");
    b.add("<h1>RF-UPS-Monitor ");
    b.addEscaped(st.version ? st.version : "");
    b.add("</h1>");
    message(b, text, error);

    char num[48];
    b.add("<h2>Status</h2><table>");
    row(b, "Mains power", !st.mainsKnown ? "unknown" : st.mainsPresent ? "present" : "LOST");
    TextBuffer nb(num, sizeof(num));
    nb.addUnsigned(st.mainsChanges);
    row(b, "Mains changes since boot", num);
    nb = TextBuffer(num, sizeof(num));
    uint32_t up = st.uptimeSeconds;
    nb.addUnsigned(up / 86400);
    nb.add(" d ");
    nb.addUnsigned(up / 3600 % 24);
    nb.add(" h ");
    nb.addUnsigned(up / 60 % 60);
    nb.add(" min");
    row(b, "Uptime", num);
    nb = TextBuffer(num, sizeof(num));
    int32_t t = st.boardTempDeciC;
    if (t < 0) nb.add("-");
    uint32_t at = (uint32_t)(t < 0 ? -t : t);
    nb.addUnsigned(at / 10);
    nb.add(".");
    nb.addUnsigned(at % 10);
    nb.add(" \xC2\xB0" "C");
    row(b, "Board temperature", num);
    nb = TextBuffer(num, sizeof(num));
    if (st.linkUp) {
        nb.add("up, ");
        nb.addUnsigned(st.linkMbps);
        nb.add(" Mbit/s");
    } else {
        nb.add("down");
    }
    row(b, "Ethernet link", num);
    row(b, "Hostname", st.hostname);
    nb = TextBuffer(num, sizeof(num));
    nb.add(st.hostname);
    nb.add(".local");
    row(b, "mDNS name", num);
    row(b, "IP address", st.ip);
    row(b, "MAC address", st.mac);
    row(b, "Network mode", st.dhcp ? "DHCP" : "static");
    b.add("</table>");

    b.add("<form method=\"post\" action=\"/network\"><fieldset><legend>Network (saving restarts the device)"
          "</legend>");
    csrfField(b, csrf);
    b.add("<label><input type=\"radio\" name=\"mode\" value=\"dhcp\"");
    if (st.dhcp) b.add(" checked");
    b.add("> DHCP</label><label><input type=\"radio\" name=\"mode\" value=\"static\"");
    if (!st.dhcp) b.add(" checked");
    b.add("> Static</label>");
    textInput(b, "IP address", "ip", st.staticIP, 15);
    textInput(b, "Subnet mask", "mask", st.subnetMask, 15);
    textInput(b, "Gateway", "gateway", st.gateway, 15);
    b.add("<button type=\"submit\">Save and restart</button></fieldset></form>");

    b.add("<form method=\"post\" action=\"/community\"><fieldset><legend>SNMP community (active immediately)"
          "</legend>");
    csrfField(b, csrf);
    textInput(b, "Community", "community", st.community, COMMUNITY_MAX_LENGTH);
    b.add("<button type=\"submit\">Save</button></fieldset></form>");

    b.add("<form method=\"post\" action=\"/hostname\"><fieldset><legend>Device name (active immediately)"
          "</legend>");
    csrfField(b, csrf);
    textInput(b, "Hostname (also SNMP sysName and name.local)", "hostname", st.hostname, HOSTNAME_MAX_LENGTH);
    b.add("<button type=\"submit\">Save</button></fieldset></form>");

    b.add("<form class=\"inline\" method=\"post\" action=\"/reboot\">");
    csrfField(b, csrf);
    b.add("<button type=\"submit\">Restart device</button></form> "
          "<form class=\"inline\" method=\"post\" action=\"/logout\">");
    csrfField(b, csrf);
    b.add("<button type=\"submit\">Log out</button></form>");
    pageEnd(b);
    return respond(code, reason, nullptr, len);
}

const char* WebApp::login(const HttpRequest& req, uint32_t sourceIP, uint32_t nowMs, WebBackend& backend, size_t& len) {
    if (!_throttle.allowed(nowMs)) {
        backend.log(sourceIP, WebEvent::LoginLocked);
        char text[80];
        TextBuffer t(text, sizeof(text));
        t.add("Too many failed logins. Try again in ");
        t.addUnsigned(_throttle.secondsLeft(nowMs));
        t.add(" s.");
        return loginPage(429, "Too Many Requests", text, len);
    }
    char user[17], password[WEB_PASSWORD_MAX + 2];
    bool haveUser = formValue(req.body(), req.bodyLength(), "user", user, sizeof(user));
    bool havePassword = formValue(req.body(), req.bodyLength(), "password", password, sizeof(password));
    // Always check the password, so a wrong user name takes as long as a wrong password
    bool passwordOk = havePassword && backend.checkPassword(password);
    memset(password, 0, sizeof(password));
    if (haveUser && webConstantTimeEquals(user, WEB_USER) && passwordOk) {
        _throttle.succeeded();
        uint8_t random[WEB_SESSION_RANDOM];
        backend.random(random, sizeof(random));
        size_t slot = _sessions.create(random, nowMs);
        memset(random, 0, sizeof(random));
        backend.log(sourceIP, WebEvent::LoggedIn);
        char cookie[128];
        TextBuffer c(cookie, sizeof(cookie));
        c.add("Set-Cookie: sid=");
        c.add(_sessions.token(slot));
        c.add("; Path=/; HttpOnly; SameSite=Strict\r\n");
        return redirect(cookie, len);
    }
    _throttle.failed(nowMs);
    backend.log(sourceIP, WebEvent::LoginFailed);
    return loginPage(401, "Unauthorized", "Wrong user name or password.", len);
}

const char* WebApp::network(const HttpRequest& req, size_t slot, WebBackend& backend, size_t& len) {
    char mode[8];
    if (!formValue(req.body(), req.bodyLength(), "mode", mode, sizeof(mode)) ||
        (strcmp(mode, "dhcp") != 0 && strcmp(mode, "static") != 0)) {
        return mainPage(400, "Bad Request", slot, backend, "Choose DHCP or static.", true, len);
    }
    bool ok;
    char ip[16], mask[16], gateway[16];
    if (strcmp(mode, "dhcp") == 0) {
        // Keep the stored static values for a later switch back
        WebStatus st;
        memset(&st, 0, sizeof(st));
        backend.status(st);
        ok = backend.saveNetwork(true, st.staticIP, st.subnetMask, st.gateway);
        strcpy(ip, "");
    } else {
        if (!formValue(req.body(), req.bodyLength(), "ip", ip, sizeof(ip)) ||
            !formValue(req.body(), req.bodyLength(), "mask", mask, sizeof(mask)) ||
            !formValue(req.body(), req.bodyLength(), "gateway", gateway, sizeof(gateway))) {
            return mainPage(400, "Bad Request", slot, backend, "Enter IPv4 addresses such as 192.168.1.10.", true, len);
        }
        StaticConfigError err = validateStaticConfig(ip, mask, gateway);
        if (err != StaticConfigError::None) {
            return mainPage(400, "Bad Request", slot, backend, staticConfigErrorMessage(err), true, len);
        }
        ok = backend.saveNetwork(false, ip, mask, gateway);
    }
    if (!ok) return mainPage(500, "Internal Server Error", slot, backend, "Saving failed, nothing changed.", true, len);
    backend.requestReboot();
    char text[160];
    TextBuffer t(text, sizeof(text));
    if (ip[0]) {
        t.add("The settings are saved and the device restarts now. It will be reachable at ");
        t.add(ip);
        t.add(" in a few seconds; log in again there.");
    } else {
        t.add("The settings are saved and the device restarts now with DHCP. Find its new address in your "
              "DHCP server or on the serial status screen.");
    }
    return notice(200, "OK", "Restarting", text, len);
}

const char* WebApp::community(const HttpRequest& req, size_t slot, WebBackend& backend, size_t& len) {
    char value[COMMUNITY_MAX_LENGTH + 2];
    if (!formValue(req.body(), req.bodyLength(), "community", value, sizeof(value)) ||
        !settingsTextIsValid(value, COMMUNITY_MAX_LENGTH, false)) {
        return mainPage(400, "Bad Request", slot, backend,
                        "The community must be 1-31 printable characters.", true, len);
    }
    if (!backend.saveCommunity(value)) {
        return mainPage(500, "Internal Server Error", slot, backend, "Saving failed, nothing changed.", true, len);
    }
    return mainPage(200, "OK", slot, backend, "SNMP community saved and active.", false, len);
}

const char* WebApp::hostname(const HttpRequest& req, size_t slot, WebBackend& backend, size_t& len) {
    char value[HOSTNAME_MAX_LENGTH + 2];
    if (!formValue(req.body(), req.bodyLength(), "hostname", value, sizeof(value)) || !hostnameIsValid(value)) {
        return mainPage(400, "Bad Request", slot, backend,
                        "The hostname must be 1-32 letters, digits and '-', not starting or ending with '-'.", true, len);
    }
    if (!backend.saveHostname(value)) {
        return mainPage(500, "Internal Server Error", slot, backend, "Saving failed, nothing changed.", true, len);
    }
    return mainPage(200, "OK", slot, backend, "Hostname saved and active.", false, len);
}

const char* WebApp::handle(const HttpRequest& req, uint32_t sourceIP, uint32_t nowMs, WebBackend& backend,
                           size_t& len) {
    if (req.state() == HttpParse::TooLarge) {
        return notice(413, "Payload Too Large", "Request too large", "The request was too large.", len);
    }
    if (req.state() != HttpParse::Complete) {
        return notice(400, "Bad Request", "Bad request", "The request could not be understood.", len);
    }

    int slot = -1;
    char token[WEB_TOKEN_HEX + 2];
    if (req.header("Cookie", _cookies, sizeof(_cookies)) && cookieValue(_cookies, "sid", token, sizeof(token))) {
        slot = _sessions.find(token, nowMs);
    }

    const char* path = req.path();
    bool get = req.method() == HttpMethod::Get, post = req.method() == HttpMethod::Post;

    if (strcmp(path, "/") == 0) {
        if (!get) return notice(405, "Method Not Allowed", "Not allowed", "Use GET for this page.", len);
        if (slot < 0) return loginPage(200, "OK", nullptr, len);
        return mainPage(200, "OK", (size_t)slot, backend, nullptr, false, len);
    }
    if (strcmp(path, "/login") == 0) {
        if (get) return redirect(nullptr, len);
        if (!post) return notice(405, "Method Not Allowed", "Not allowed", "Use POST for this page.", len);
        return login(req, sourceIP, nowMs, backend, len);
    }

    bool action = strcmp(path, "/logout") == 0 || strcmp(path, "/network") == 0 ||
                  strcmp(path, "/community") == 0 || strcmp(path, "/reboot") == 0 ||
                  strcmp(path, "/hostname") == 0;
    if (!action) return notice(404, "Not Found", "Not found", "There is no such page.", len);
    if (!post) return notice(405, "Method Not Allowed", "Not allowed", "Use the buttons on the start page.", len);
    if (slot < 0) {
        return notice(403, "Forbidden", "Not logged in", "Your session has ended. Log in again.", len);
    }
    char csrf[WEB_TOKEN_HEX + 2];
    if (!formValue(req.body(), req.bodyLength(), "csrf", csrf, sizeof(csrf)) ||
        !webConstantTimeEquals(csrf, _sessions.csrf((size_t)slot))) {
        return notice(403, "Forbidden", "Form expired", "The form was out of date. Reload the page and try again.", len);
    }

    if (strcmp(path, "/logout") == 0) {
        _sessions.remove((size_t)slot);
        return redirect("Set-Cookie: sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict\r\n", len);
    }
    if (strcmp(path, "/network") == 0) return network(req, (size_t)slot, backend, len);
    if (strcmp(path, "/community") == 0) return community(req, (size_t)slot, backend, len);
    if (strcmp(path, "/hostname") == 0) return hostname(req, (size_t)slot, backend, len);
    backend.requestReboot();
    return notice(200, "OK", "Restarting", "The device restarts now. Log in again in a few seconds.", len);
}
