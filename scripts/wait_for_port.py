"""PlatformIO post-upload hook: wait for the Pico's USB serial port to return.

After flashing, the Pico reboots and its USB CDC port disappears for 1-2 s.
`pio test` opens the port immediately and fails with "could not open port".
This waits until the port is listed again before the test runner continues.
"""

import time

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

TIMEOUT_S = 15
SETTLE_S = 1.0


def wait_for_port(source, target, env):
    from serial.tools import list_ports

    port = env.GetProjectOption("test_port", "") or env.GetProjectOption("upload_port", "")
    if not port:
        return

    deadline = time.time() + TIMEOUT_S
    time.sleep(SETTLE_S)  # Let the old port vanish first
    while time.time() < deadline:
        if any(p.device.upper() == port.upper() for p in list_ports.comports()):
            time.sleep(SETTLE_S)
            return
        time.sleep(0.2)
    print("wait_for_port: %s did not reappear within %d s" % (port, TIMEOUT_S))


env.AddPostAction("upload", wait_for_port)  # noqa: F821
