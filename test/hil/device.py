"""Serial-port driver for the RF-UPS-Monitor menu, used by the HIL tests.

Talks to the device exactly like a person in PuTTY: sends a line terminated
by CR and reads what comes back.
"""

import os
import re
import subprocess
import sys
import time

import serial
from serial.tools import list_ports

PORT = os.environ.get("RFUPS_PORT", "COM16")
BAUD = 115200
PROMPT = "> "


class Device:
    """One serial session with the device."""

    def __init__(self, port=PORT):
        self.port = port
        self.ser = None
        self.log = ""

    # ---- connection ----

    def wait_for_port(self, timeout=20.0):
        """Wait until the USB serial port is listed (it vanishes during reboot)."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if any(p.device.upper() == self.port.upper() for p in list_ports.comports()):
                return
            time.sleep(0.2)
        raise TimeoutError("%s did not appear within %.0f s" % (self.port, timeout))

    def open(self):
        self.wait_for_port()
        deadline = time.time() + 10
        while True:
            try:
                # DTR must be set, the firmware waits for it before printing
                self.ser = serial.Serial(self.port, BAUD, timeout=0.1, dsrdtr=False)
                self.ser.dtr = True
                return
            except serial.SerialException as e:
                if time.time() > deadline:
                    raise RuntimeError(
                        "cannot open %s (is PuTTY or a serial monitor using it?): %s" % (self.port, e))
                time.sleep(0.5)

    def close(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except serial.SerialException:
                pass
            self.ser = None

    # ---- I/O ----

    def read_until(self, pattern, timeout=5.0):
        """Read until the regex pattern appears; return everything read."""
        regex = re.compile(pattern)
        buf = ""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                chunk = self.ser.read(256)
            except serial.SerialException:
                break
            if chunk:
                buf += chunk.decode("utf-8", errors="replace")
                if regex.search(buf):
                    self.log += buf
                    return buf
        self.log += buf
        raise TimeoutError("pattern %r not seen within %.0f s, got:\n%s" % (pattern, timeout, buf))

    def send(self, line, expect=None, timeout=5.0):
        """Send one line. If expect is given, read until it appears and return the output."""
        self.ser.write((line + "\r").encode())
        if expect is None:
            return ""
        return self.read_until(expect, timeout)

    def wait_for_menu(self, timeout=40.0):
        """Wait for a prompt, nudging with '?' since the menu may have printed before we connected."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.ser.reset_input_buffer()
            self.ser.write(b"?\r")
            try:
                return self.read_until(re.escape(PROMPT) + r"$", timeout=3.0)
            except TimeoutError:
                continue  # Still booting or waiting for DHCP
        raise TimeoutError("menu prompt not seen within %.0f s" % timeout)

    def reconnect_after_reboot(self, timeout=40.0):
        """Close, wait for the port to drop and return, reopen and wait for the menu."""
        self.close()
        time.sleep(1.5)
        self.open()
        return self.wait_for_menu(timeout)

    # ---- menu helpers ----

    def status(self):
        """Run menu option 1 and return a dict with mode, ip, subnet, gateway."""
        out = self.send("1", expect=r"DNS: .*\r?\n" + re.escape(PROMPT))
        info = {"raw": out}
        m = re.search(r"Mode: (\w+)", out)
        info["mode"] = m.group(1) if m else None
        for key, label in (("ip", "IP"), ("subnet", "Subnet"), ("gateway", "Gateway")):
            m = re.search(r"^%s: ([\d.]+)" % label, out, re.MULTILINE)
            info[key] = m.group(1) if m else None
        return info


def ping(host, count=2):
    """Ping from the PC running the tests; True if any reply arrives."""
    flag = "-n" if sys.platform == "win32" else "-c"
    result = subprocess.run(["ping", flag, str(count), host],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return result.returncode == 0 and ("TTL=" in result.stdout or "ttl=" in result.stdout)
