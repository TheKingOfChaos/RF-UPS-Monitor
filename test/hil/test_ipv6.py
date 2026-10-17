"""Hardware-in-the-loop tests for IPv6 (dual stack).

The device always has a link-local address (fe80::, from its MAC). If a router
on the LAN announces a prefix, it also gets a SLAAC address; the tests use it
when present. The PC needs IPv6 on the interface towards the device.

Run:
    %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe -m unittest discover -s test/hil -p "test_ipv6.py" -v
"""

import http.client
import ipaddress
import re
import socket
import subprocess
import sys
import time
import unittest

from device import Device
from snmp_tools import GET_SYSDESCR, local_ip_towards, net_snmp

SNMP_PORT = 161


def bad_community_request():
    pkt = bytearray(GET_SYSDESCR)
    pkt[7:13] = b"wrongc"
    return bytes(pkt)


def interface_index_towards(ipv4):
    """Windows interface index (scope id for fe80:: addresses) of the interface with this PC address."""
    out = subprocess.run(["powershell", "-NoProfile", "-Command",
                          "(Get-NetIPAddress -AddressFamily IPv4 -IPAddress %s).InterfaceIndex" % ipv4],
                         capture_output=True, text=True).stdout.strip()
    return int(out) if out.isdigit() else 0


class Ipv6Test(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.dev = Device()
        cls.dev.open()
        cls.dev.wait_for_menu()
        st = cls.dev.wait_connected()
        if st["community"] != "public":
            cls.dev.close()
            raise unittest.SkipTest("device community must be public")
        cls.ip4 = st["ip"]
        cls.pc4 = local_ip_towards(cls.ip4)
        cls.scope = interface_index_towards(cls.pc4) if sys.platform == "win32" else 0
        cls.start_ticks = cls.uptime_ticks()
        cls.start_time = time.time()
        cls.addresses = cls.ipv6_addresses()

    @classmethod
    def tearDownClass(cls):
        cls.dev.close()

    @classmethod
    def ipv6_addresses(cls, timeout=20):
        """IPv6 addresses from the status screen, waiting until duplicate detection is over."""
        deadline = time.time() + timeout
        while True:
            raw = cls.dev.status()["raw"]
            addrs = re.findall(r"^IPv6: ([0-9A-F:]+)(?: \((tentative|deprecated)\))?\r?$", raw, re.MULTILINE)
            ready = [a.lower() for a, state in addrs if state == ""]
            if any(a.startswith("fe80:") for a in ready) or time.time() > deadline:
                return ready
            time.sleep(1)

    @classmethod
    def uptime_ticks(cls):
        """sysUpTime (1/100 s) over IPv4"""
        rc, out = net_snmp("snmpget", cls.ip4, "1.3.6.1.2.1.1.3.0", version="2c")
        m = re.search(r"Timeticks: \((\d+)\)", out)
        assert m, out
        return int(m.group(1))

    def setUp(self):
        self.dev.wait_for_menu(timeout=10)

    def link_local(self):
        ll = [a for a in self.addresses if a.startswith("fe80:")]
        self.assertTrue(ll, "no link-local IPv6 address on the status screen")
        return ll[0]

    def target(self):
        """(address, scope id) to reach the device: global if it has one, else link-local."""
        glob = [a for a in self.addresses if not a.startswith("fe80:")]
        if glob:
            return glob[0], 0
        return self.link_local(), self.scope

    def snmp_get_v6(self, packet, timeout=1.0):
        addr, scope = self.target()
        with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as s:
            s.settimeout(timeout)
            s.sendto(packet, (addr, SNMP_PORT, 0, scope))
            try:
                data, _ = s.recvfrom(2048)
                return data
            except socket.timeout:
                return None

    # ---- addresses ----

    def test_link_local_address_comes_from_the_mac(self):
        mac = self.dev.status()["mac"].split(":")
        b = [int(x, 16) for x in mac]
        b[0] ^= 0x02  # EUI-64: flip the universal/local bit, insert ff:fe
        expected = "fe80::%x:%x:%x:%x" % (b[0] << 8 | b[1], b[2] << 8 | 0xff, 0xfe << 8 | b[3], b[4] << 8 | b[5])
        self.assertEqual(expected, self.link_local())

    def test_global_address_from_router_advertisement(self):
        glob = [a for a in self.addresses if not a.startswith("fe80:")]
        if not glob:
            self.skipTest("no IPv6 router on this LAN (only link-local)")
        self.assertTrue(glob[0].endswith(self.link_local()[len("fe80::"):]), "SLAAC address must use the EUI-64 id")

    def test_device_answers_ping6(self):
        addr, scope = self.target()
        dest = "%s%%%d" % (addr, scope) if scope else addr
        flag = "-n" if sys.platform == "win32" else "-c"
        out = subprocess.run(["ping", "-6", flag, "3", dest], capture_output=True, text=True).stdout
        self.assertIn("Reply from" if sys.platform == "win32" else "bytes from", out, out)

    # ---- services over IPv6 ----

    def test_snmp_get_over_ipv6(self):
        resp = self.snmp_get_v6(GET_SYSDESCR)
        self.assertIsNotNone(resp, "no SNMP answer over IPv6")
        self.assertIn(b"RF-UPS-Monitor", resp)

    def pc_address_v6(self):
        """The PC's own IPv6 address towards the device (what the device sees as the source)."""
        addr, scope = self.target()
        with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as s:
            s.connect((addr, SNMP_PORT, 0, scope))
            return ipaddress.ip_address(s.getsockname()[0].split("%")[0])

    def test_wrong_community_over_ipv6_is_logged_with_the_full_pc_address(self):
        self.assertIsNone(self.snmp_get_v6(bad_community_request(), timeout=0.5))
        log = self.dev.access_log()
        v6 = [e for e in log if ":" in e[0] and e[1] == "bad community"]
        self.assertTrue(v6, "IPv6 source not in the access log: %s" % log)
        # The whole address, not only the /64 (trust and rate limits are per address)
        self.assertEqual(self.pc_address_v6(), ipaddress.ip_address(v6[0][0]), v6)

    def test_web_page_over_ipv6(self):
        addr, scope = self.target()
        host = "[%s%s]" % (addr, "%%25%d" % scope if scope else "")
        conn = http.client.HTTPConnection(addr if not scope else "%s%%%d" % (addr, scope), 80, timeout=5)
        try:
            conn.request("GET", "/", headers={"Host": host})
            r = conn.getresponse()
            page = r.read().decode()
        finally:
            conn.close()
        self.assertEqual(200, r.status)
        self.assertIn('action="/login"', page)

    def test_mdns_name_resolves_to_ipv6(self):
        name = self.dev.status()["hostname"] + ".local"
        try:
            infos = socket.getaddrinfo(name, 80, socket.AF_INET6)
        except socket.gaierror as e:
            self.skipTest("this PC does not resolve %s over IPv6: %s" % (name, e))
        found = {i[4][0].split("%")[0].lower() for i in infos}
        self.assertTrue(found & set(self.addresses), "%s -> %s, device has %s" % (name, found, self.addresses))

    def test_back_to_back_requests_are_answered_and_do_not_crash(self):
        """Two queued UDP packets crashed the first IPv6 build (core UdpContext helper pbuf, LWIP_IPV6_FRAG)."""
        addr, scope = self.target()
        for family, dest in ((socket.AF_INET, (self.ip4, SNMP_PORT)), (socket.AF_INET6, (addr, SNMP_PORT, 0, scope))):
            with socket.socket(family, socket.SOCK_DGRAM) as s:
                s.settimeout(1)
                for _ in range(4):
                    s.sendto(GET_SYSDESCR, dest)  # all four before reading any reply
                answers = 0
                try:
                    while answers < 4:
                        s.recvfrom(2048)
                        answers += 1
                except socket.timeout:
                    pass
            self.assertEqual(4, answers, "family %s: %d of 4 answered" % (family, answers))
        ticks = self.uptime_ticks() - self.start_ticks
        self.assertGreater(ticks, (time.time() - self.start_time) * 100 * 0.9, "the device restarted")

    def test_ipv4_still_works(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.settimeout(1)
            s.sendto(GET_SYSDESCR, (self.ip4, SNMP_PORT))
            data, _ = s.recvfrom(2048)
        self.assertIn(b"RF-UPS-Monitor", data)

    def test_zz_device_did_not_restart_during_the_tests(self):
        # The first IPv6 build panicked seconds after boot (lwIP timer pool empty)
        time.sleep(20)  # keep IPv6 and mDNS traffic going a little longer
        self.assertIsNotNone(self.snmp_get_v6(GET_SYSDESCR))
        elapsed = time.time() - self.start_time
        ticks = self.uptime_ticks() - self.start_ticks
        self.assertGreater(ticks, elapsed * 100 * 0.9, "sysUpTime went back: the device restarted")

if __name__ == "__main__":
    unittest.main(verbosity=2)
