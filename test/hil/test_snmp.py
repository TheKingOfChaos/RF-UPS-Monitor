"""Hardware-in-the-loop tests for the SNMP v1 agent on the flashed device.

Uses the real net-snmp tools (snmpget, snmpgetnext, snmpwalk, snmpset) in
WSL, and raw UDP from Windows for timing and malformed-packet floods. The
device IP is read from the serial status screen, so the serial port must be
free (close PuTTY).

Run (from the project root, firmware flashed):
    %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe -m unittest discover -s test/hil -p "test_snmp.py" -v

Environment variables:
    RFUPS_PORT          Serial port, default COM16
    RFUPS_WSL_DISTRO    WSL distribution with net-snmp, default Ubuntu-24.04
    RFUPS_MANUAL_MAINS  Set to 1 for the interactive mains off/on test

The device must use community "public"; tests that change the SNMP
settings restore them afterwards.
"""

import os
import re
import socket
import statistics
import struct
import time
import unittest

from device import Device
from snmp_tools import (GET_SYSDESCR, REQUEST_ID_OFFSET, garbage_packets, local_ip_towards, net_snmp,
                        raw_get, response_request_id, send_raw)

ENTERPRISE = "1.3.6.1.4.1.63050"
NO_SUCH = r"\(noSuchName\) There is no such variable name in this MIB\.\s+Failed object: "


class SnmpAgentTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.dev = Device()
        cls.dev.open()
        cls.dev.wait_for_menu()
        cls.ip = cls.dev.wait_connected()["ip"]
        community = cls.dev.status()["community"]
        if community != "public":
            cls.dev.close()
            raise unittest.SkipTest('device community is "%s"; set it to "public" (menu option 4) '
                                    "before running the SNMP HIL tests" % community)

    @classmethod
    def tearDownClass(cls):
        cls.dev.close()

    def snmp(self, tool, *args, **kw):
        return net_snmp(tool, self.ip, *args, **kw)

    def snmp_stats(self):
        """SNMP counters from the serial status screen."""
        self.dev.wait_for_menu(timeout=10)
        raw = self.dev.status()["raw"]
        m = re.search(r"SNMP: (\d+) packets, (\d+) responses \(get (\d+), getnext (\d+), set (\d+)\), "
                      r"bad community (\d+), bad version (\d+), malformed (\d+)", raw)
        self.assertIsNotNone(m, raw)
        keys = ("packets", "responses", "get", "getnext", "set", "bad_community", "bad_version", "malformed")
        return dict(zip(keys, map(int, m.groups())))

    # ---- GetRequest ----

    def test_get_sysdescr(self):
        rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.1.0")
        self.assertEqual(0, rc, out)
        self.assertRegex(out, r'\.1\.3\.6\.1\.2\.1\.1\.1\.0 = STRING: "RF-UPS-Monitor v\d+\.\d+\.\d+ ')

    def test_get_sysobjectid_is_enterprise(self):
        rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.2.0")
        self.assertIn("OID: ." + ENTERPRISE, out)

    def test_sysuptime_advances(self):
        def ticks():
            rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.3.0")
            m = re.search(r"Timeticks: \((\d+)\)", out)
            self.assertIsNotNone(m, out)
            return int(m.group(1))
        t1 = ticks()
        time.sleep(1.0)
        t2 = ticks()
        self.assertGreaterEqual(t2 - t1, 90, "sysUpTime advanced %d ticks in ~1 s" % (t2 - t1))
        self.assertLess(t2 - t1, 300)

    def test_mains_status_matches_serial_status(self):
        rc, out = self.snmp("snmpget", ENTERPRISE + ".1.0", ENTERPRISE + ".2.0", ENTERPRISE + ".3.0")
        self.assertEqual(0, rc, out)
        m = re.search(r"63050\.1\.0 = INTEGER: (\d)", out)
        self.assertIsNotNone(m, out)
        self.assertRegex(out, r"63050\.2\.0 = Counter32: \d+")
        self.assertRegex(out, r"63050\.3\.0 = Timeticks: \(\d+\)")
        serial = self.dev.status()["mains"]
        self.assertEqual("present" if m.group(1) == "1" else "LOST", serial)

    def test_get_several_varbinds_in_request_order(self):
        rc, out = self.snmp("snmpget", ENTERPRISE + ".1.0", "1.3.6.1.2.1.1.5.0", "1.3.6.1.2.1.1.7.0")
        self.assertEqual(0, rc, out)
        lines = [l for l in out.splitlines() if " = " in l]
        self.assertEqual(3, len(lines), out)
        self.assertTrue(lines[0].startswith("." + ENTERPRISE + ".1.0"))
        self.assertIn('STRING: "%s"' % self.dev.status()["sysName"], lines[1])
        self.assertIn("INTEGER: 72", lines[2])

    def test_get_unknown_oid_is_no_such_name(self):
        rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.99.0")
        self.assertNotEqual(0, rc)
        self.assertRegex(out, NO_SUCH + r"\.1\.3\.6\.1\.2\.1\.1\.99\.0")

    # ---- GetNextRequest / walk ----

    def test_walk_returns_all_objects_in_order(self):
        rc, out = self.snmp("snmpwalk", ".1")
        self.assertEqual(0, rc, out)
        oids = [l.split(" = ")[0] for l in out.splitlines() if " = " in l]
        expected = [".1.3.6.1.2.1.1.%d.0" % i for i in range(1, 8)] + \
                   ["." + ENTERPRISE + ".%d.0" % i for i in range(1, 4)]
        self.assertEqual(expected, oids)
        self.assertIn("End of MIB", out)

    def test_walk_enterprise_subtree(self):
        rc, out = self.snmp("snmpwalk", ENTERPRISE)
        self.assertEqual(3, sum(1 for l in out.splitlines() if " = " in l), out)

    def test_walk_system_group(self):
        rc, out = self.snmp("snmpwalk", "1.3.6.1.2.1.1")
        self.assertEqual(7, sum(1 for l in out.splitlines() if " = " in l), out)

    def test_getnext_past_end(self):
        rc, out = self.snmp("snmpgetnext", ENTERPRISE + ".3.0")
        self.assertRegex(out, NO_SUCH + r"\." + ENTERPRISE.replace(".", r"\.") + r"\.3\.0")

    # ---- rejected requests ----

    def test_set_is_rejected_and_changes_nothing(self):
        rc, out = self.snmp("snmpset", "1.3.6.1.2.1.1.5.0", "s", "hacked")
        self.assertNotEqual(0, rc)
        self.assertRegex(out, NO_SUCH + r"\.1\.3\.6\.1\.2\.1\.1\.5\.0")
        rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.5.0")
        self.assertIn('STRING: "%s"' % self.dev.status()["sysName"], out)

    def test_wrong_community_gets_no_reply_and_is_counted(self):
        before = self.snmp_stats()["bad_community"]
        rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.1.0", community="not-public", timeout=1)
        self.assertIn("Timeout", out)
        self.assertEqual(before + 1, self.snmp_stats()["bad_community"])

    def test_v2c_gets_no_reply_and_is_counted(self):
        before = self.snmp_stats()["bad_version"]
        rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.1.0", version="2c", timeout=1)
        self.assertIn("Timeout", out)
        self.assertEqual(before + 1, self.snmp_stats()["bad_version"])

    # ---- SNMP settings from the serial menu ----

    def test_snmp_settings_from_menu_apply_immediately(self):
        d = self.dev
        before = d.status()
        packets_before = self.snmp_stats()["packets"]
        try:
            out = d.set_snmp("hil-community", "hil-name", "hil@example.com", 'HIL bench "A"')
            self.assertIn("SNMP settings saved and active", out)

            rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.5.0", "1.3.6.1.2.1.1.4.0", "1.3.6.1.2.1.1.6.0",
                                community="hil-community")
            self.assertEqual(0, rc, out)
            self.assertIn('1.5.0 = STRING: "hil-name"', out)
            self.assertIn('1.4.0 = STRING: "hil@example.com"', out)
            self.assertIn('1.6.0 = STRING: "HIL bench \\"A\\""', out)

            rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.5.0", community=before["community"], timeout=1)
            self.assertIn("Timeout", out, "old community must be refused after the change")

            # No reboot happened: the packet counter kept counting instead of restarting at 0
            self.assertGreater(self.snmp_stats()["packets"], packets_before)
        finally:
            d.wait_for_menu(timeout=10)
            d.set_snmp(before["community"], before["sysName"], before["sysContact"], before["sysLocation"])
        rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.5.0", community=before["community"])
        self.assertIn('STRING: "%s"' % before["sysName"], out)

    def test_snmp_settings_survive_reboot(self):
        d = self.dev
        before = d.status()
        try:
            d.set_snmp(before["community"], "persist-test", "", "")
            d.send("2", expect=r"\(y/n\): $")                  # DHCP + reboot, keeps other settings
            d.send("y", expect=r"Rebooting", timeout=10)
            d.reconnect_after_reboot()
            self.assertEqual("persist-test", d.status()["sysName"])
            rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.5.0", community=before["community"])
            self.assertIn('STRING: "persist-test"', out)
        finally:
            d.wait_for_menu(timeout=10)
            d.set_snmp(before["community"], before["sysName"], before["sysContact"], before["sysLocation"])

    # ---- performance and robustness ----

    def test_response_time_under_100ms(self):
        times = []
        for i in range(30):
            resp, secs = raw_get(self.ip, 1000 + i)
            self.assertIsNotNone(resp, "no response to request %d" % i)
            self.assertEqual(1000 + i, response_request_id(resp), "request id not echoed")
            times.append(secs * 1000)
        median, worst = statistics.median(times), max(times)
        print("\n    SNMP round trip: median %.1f ms, max %.1f ms" % (median, worst))
        self.assertLess(worst, 100.0, ".clinerules: SNMP requests must be handled within 100 ms")

    def test_survives_malformed_packet_flood(self):
        # The core's UDP queue holds 5 packets, so most of each burst is dropped
        # before the agent; what matters is that the agent keeps working.
        before = self.snmp_stats()
        packets = garbage_packets(300)
        for i in range(0, len(packets), 20):   # bursts of 20
            send_raw(self.ip, packets[i:i + 20])
            time.sleep(0.05)
        time.sleep(2.5)                         # let the queue drain and the rate limiter refill
        answered = any(raw_get(self.ip, 77 + k, timeout=0.5)[0] is not None for k in range(3))
        self.assertTrue(answered, "agent stopped answering after malformed packets")
        after = self.snmp_stats()
        self.assertGreater(after["malformed"], before["malformed"], "flood never reached the agent")
        self.assertGreater(after["responses"], before["responses"], "probe response not counted")

    # ---- security: rate limiting and access log ----

    def test_rate_limit_caps_one_source_and_recovers(self):
        # 50 requests/s per source with a burst of 100. Send as fast as replies come back for 3 s.
        time.sleep(2.5)                          # start with a full bucket
        before = self.dev.status()["rateLimited"]
        answered = unanswered = 0
        start = time.time()
        while time.time() - start < 3.0:
            resp, _ = raw_get(self.ip, 7000 + answered + unanswered, timeout=0.05)
            if resp is None:
                unanswered += 1
            else:
                answered += 1
        elapsed = time.time() - start
        print("\n    rate limit: %d answered, %d dropped in %.1f s" % (answered, unanswered, elapsed))
        expected = 100 + 50 * elapsed
        self.assertGreater(unanswered, 0, "nothing was rate limited")
        self.assertLess(answered, expected * 1.15, "more answered than the limit allows")
        self.assertGreater(answered, expected * 0.7, "far fewer answered than the limit allows")

        st = self.dev.status()
        self.assertGreater(st["rateLimited"], before)
        entries = [e for e in self.dev.access_log() if e[1] == "rate limited"]
        self.assertTrue(entries, "rate limiting not in the access log")

        time.sleep(2.5)                          # bucket refills
        self.assertIsNotNone(raw_get(self.ip, 1, timeout=0.5)[0], "source still blocked after refill")

    def test_access_log_lists_rejected_requests_with_source(self):
        pc = local_ip_towards(self.ip)
        bad = bytearray(GET_SYSDESCR)
        bad[7:13] = b"wrongc"
        send_raw(self.ip, [bytes(bad)])
        rc, out = self.snmp("snmpget", "1.3.6.1.2.1.1.1.0", version="2c", timeout=1)
        time.sleep(0.3)
        log = self.dev.access_log()
        events = {(ip, ev) for ip, ev, _ in log}
        self.assertIn((pc, "bad community"), events, log)
        self.assertIn((pc, "bad version"), events, log)
        newest = log[0]
        self.assertEqual((pc, "bad version"), newest[:2], "newest entry must come first")

    def test_set_request_is_logged(self):
        self.snmp("snmpset", "1.3.6.1.2.1.1.5.0", "s", "x")
        log = self.dev.access_log()
        self.assertEqual("set rejected", log[0][1], log)

    def test_five_simultaneous_requests_are_all_answered(self):
        # Documented limit: the core queues 5 UDP packets (UdpContext::rxBufMaxDepth + 1).
        # Warm up first: if the device's ARP entry for this PC has expired, lwIP holds
        # only one reply while it resolves the address and drops the others.
        self.assertIsNotNone(raw_get(self.ip, 499)[0], "warm-up request not answered")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.settimeout(0.5)
            for i in range(5):
                pkt = bytearray(GET_SYSDESCR)
                struct.pack_into(">I", pkt, REQUEST_ID_OFFSET, 500 + i)
                s.sendto(bytes(pkt), (self.ip, 161))
            ids = set()
            try:
                while True:
                    ids.add(response_request_id(s.recvfrom(2048)[0]))
            except socket.timeout:
                pass
        self.assertEqual({500, 501, 502, 503, 504}, ids)

    def test_oversized_packet_is_ignored(self):
        send_raw(self.ip, [b"\x30" + b"\x00" * 1499])
        resp, _ = raw_get(self.ip, 78)
        self.assertIsNotNone(resp)

    # ---- manual ----

    @unittest.skipUnless(os.environ.get("RFUPS_MANUAL_MAINS"),
                         "manual: set RFUPS_MANUAL_MAINS=1 and switch mains off and on when asked")
    def test_mains_loss_and_restore_visible_over_snmp(self):
        def mains():
            rc, out = self.snmp("snmpget", ENTERPRISE + ".1.0", ENTERPRISE + ".2.0", ENTERPRISE + ".3.0")
            status = int(re.search(r"63050\.1\.0 = INTEGER: (\d)", out).group(1))
            changes = int(re.search(r"Counter32: (\d+)", out).group(1))
            last = int(re.search(r"63050\.3\.0 = Timeticks: \((\d+)\)", out).group(1))
            return status, changes, last

        status, changes, last = mains()
        self.assertEqual(1, status, "mains must be on when the test starts")

        def wait_for(expected):
            deadline = time.time() + 60
            while time.time() < deadline:
                s = mains()
                if s[0] == expected:
                    return s
                time.sleep(0.5)
            self.fail("mainsPowerStatus never became %d" % expected)

        print("\n>>> Switch mains power OFF now (60 s)...", flush=True)
        off = wait_for(0)
        self.assertEqual(changes + 1, off[1])
        self.assertGreater(off[2], last)
        print(">>> Switch mains power ON again (60 s)...", flush=True)
        on = wait_for(1)
        self.assertEqual(changes + 2, on[1])
        self.assertGreater(on[2], off[2])


if __name__ == "__main__":
    unittest.main(verbosity=2)
