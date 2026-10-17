"""Switch or read an outlet on an IKEA Dirigera hub.

Runs with the Python that has the `dirigera` package installed (a venv in
WSL here); mains_switch.py calls it. Prints the outlet state after the
command as "on" or "off".

    python dirigera_outlet.py --ip 10.10.6.124 --token-file ~/dirigera/token --outlet "Stikprop 1" off
"""

import argparse
import os
import sys

import dirigera


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--ip", required=True, help="hub address")
    p.add_argument("--token-file", required=True, help="file with the hub token (dirigera generate-token)")
    p.add_argument("--outlet", required=True, help="outlet name as shown in the IKEA app")
    p.add_argument("command", choices=("on", "off", "status"))
    a = p.parse_args()

    token = open(os.path.expanduser(a.token_file)).read().strip()
    hub = dirigera.Hub(token=token, ip_address=a.ip)
    outlet = hub.get_outlet_by_name(a.outlet)
    if not outlet.is_reachable:
        sys.exit("outlet %r is not reachable" % a.outlet)
    if a.command != "status":
        outlet.set_on(a.command == "on")
        outlet = hub.get_outlet_by_id(outlet.id)
    print("on" if outlet.attributes.is_on else "off")


if __name__ == "__main__":
    main()
