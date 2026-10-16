"""Hardware-in-the-loop tests for SNMPv1 traps sent by the device.

The trap destination is set to this PC for the duration of the tests and
restored afterwards. Traps are received on UDP 162 here, so Windows Firewall
must allow inbound UDP for PlatformIO's Python (the "Allow" prompt the first
time it listens on the network).

Run:
    %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe -m unittest discover -s test/hil -p "test_snmp_traps.py" -v

Environment variables:
    RFUPS_MANUAL_MAINS  Set to 1 for the interactive mains off/on trap test
"""

import os
import time
import unittest

from device import Device
from snmp_tools import GET_SYSDESCR, TrapListener, local_ip_towards, raw_get, send_raw

ENTERPRISE = "1.3.6.1.4.1.63050"
AUTH_TRAP_INTERVAL = 10.0


def bad_community_request():
    pkt = bytearray(GET_SYSDESCR)
    pkt[7:13] = b"wrongc"     # same length as "public"
    return bytes(pkt)


class SnmpTrapTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.dev = Device()
        cls.dev.open()
        cls.dev.wait_for_menu()
        st = cls.dev.wait_connected()
        if st["mode"] != "DHCP" or st["community"] != "public":
            cls.dev.close()
            raise unittest.SkipTest("device must be in DHCP mode with community public")
        cls.ip = st["ip"]
        cls.original_target = st["trapTarget"]
        cls.pc = local_ip_towards(cls.ip)
        cls.listener = TrapListener()
        cls.dev.set_trap_target(cls.pc)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.dev.wait_for_menu(timeout=10)
            cls.dev.set_trap_target(cls.original_target)
        finally:
            cls.listener.close()
            cls.dev.close()

    def setUp(self):
        self.dev.wait_for_menu(timeout=10)
        self.listener.clear()

    def assertFromDevice(self, trap):
        self.assertEqual(1 - 1, trap["version"])          # SNMPv1
        self.assertEqual("public", trap["community"])
        self.assertEqual(ENTERPRISE, trap["enterprise"])
        self.assertEqual(self.ip, trap["agent"])
        self.assertEqual(self.ip, trap["source"])

    # ---- coldStart ----

    def test_cold_start_trap_after_reboot(self):
        # After a reboot the ARP table is empty: the very first packet to this PC is the trap
        d = self.dev
        d.send("2", expect=r"\(y/n\): $")
        d.send("y", expect=r"Rebooting", timeout=10)
        trap = self.listener.wait_for(lambda t: t["generic"] == 0, timeout=45)
        self.assertIsNotNone(trap, "no coldStart trap within 45 s of the reboot")
        self.assertFromDevice(trap)
        self.assertEqual(0, trap["specific"])
        self.assertEqual({}, trap["varbinds"])
        self.assertLess(trap["timestamp"], 60 * 100, "coldStart time-stamp should be shortly after boot")
        d.reconnect_after_reboot()
        self.assertEqual(1, len(self.listener.matching(lambda t: t["generic"] == 0)),
                         "exactly one coldStart per boot")

    # ---- authenticationFailure ----

    def test_wrong_community_sends_one_throttled_auth_failure_trap(self):
        time.sleep(AUTH_TRAP_INTERVAL + 0.5)      # let an earlier auth trap's throttle expire
        send_raw(self.ip, [bad_community_request()])
        trap = self.listener.wait_for(lambda t: t["generic"] == 4, timeout=3)
        self.assertIsNotNone(trap, "no authenticationFailure trap")
        self.assertFromDevice(trap)

        # More bad requests right away are logged but do not cause more traps
        for _ in range(5):
            send_raw(self.ip, [bad_community_request()])
            time.sleep(0.05)
        time.sleep(2)
        self.assertEqual(1, len(self.listener.matching(lambda t: t["generic"] == 4)))
        entries = [e for e in self.dev.access_log() if e[0] == self.pc and e[1] == "bad community"]
        self.assertTrue(entries, "bad community not in the access log")
        self.assertGreaterEqual(entries[0][2], 6)

    def test_no_traps_when_destination_is_off(self):
        d = self.dev
        try:
            d.set_trap_target("")
            self.assertEqual("", d.status()["trapTarget"])
            time.sleep(AUTH_TRAP_INTERVAL + 0.5)
            send_raw(self.ip, [bad_community_request()])
            self.assertIsNone(self.listener.wait_for(lambda t: True, timeout=3), "trap sent although off")
        finally:
            d.wait_for_menu(timeout=10)
            d.set_trap_target(self.pc)

    def test_trap_counter_on_status_screen(self):
        before = self.dev.status()["trapsSent"]
        time.sleep(AUTH_TRAP_INTERVAL + 0.5)
        send_raw(self.ip, [bad_community_request()])
        self.assertIsNotNone(self.listener.wait_for(lambda t: t["generic"] == 4, timeout=3))
        self.assertEqual(before + 1, self.dev.status()["trapsSent"])

    def test_agent_keeps_answering_while_sending_traps(self):
        time.sleep(AUTH_TRAP_INTERVAL + 0.5)
        send_raw(self.ip, [bad_community_request()])
        resp, secs = raw_get(self.ip, 4242)
        self.assertIsNotNone(resp)
        self.assertLess(secs, 1.0)

    # ---- mains (manual) ----

    @unittest.skipUnless(os.environ.get("RFUPS_MANUAL_MAINS"),
                         "manual: set RFUPS_MANUAL_MAINS=1 and switch mains off and on when asked")
    def test_mains_traps_with_real_mains(self):
        print("\n>>> Switch mains power OFF now (60 s)...", flush=True)
        lost = self.listener.wait_for(lambda t: t["generic"] == 6 and t["specific"] == 1, timeout=60)
        self.assertIsNotNone(lost, "no mainsPowerLost trap")
        self.assertFromDevice(lost)
        vb = lost["varbinds"]
        self.assertEqual((0x02, 0), vb[ENTERPRISE + ".1.0"])      # mainsPowerStatus = lost
        changes = vb[ENTERPRISE + ".2.0"][1]
        self.assertEqual(lost["timestamp"], vb[ENTERPRISE + ".3.0"][1])

        print(">>> Switch mains power ON again (60 s)...", flush=True)
        restored = self.listener.wait_for(lambda t: t["generic"] == 6 and t["specific"] == 2, timeout=60)
        self.assertIsNotNone(restored, "no mainsPowerRestored trap")
        vb = restored["varbinds"]
        self.assertEqual((0x02, 1), vb[ENTERPRISE + ".1.0"])
        self.assertEqual(changes + 1, vb[ENTERPRISE + ".2.0"][1])
        self.assertGreater(restored["timestamp"], lost["timestamp"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
