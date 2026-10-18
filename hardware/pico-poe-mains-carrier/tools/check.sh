#!/bin/bash
# Regenerate, refill zones, run ERC + DRC (with schematic parity) and summarise.
set -e
P=$(cd "$(dirname "$0")/.." && pwd)
N=pico-poe-mains-carrier
python3 "$P/tools/generate.py" >/dev/null
cd "$P"
kicad-cli sch erc --format json --severity-all -o /tmp/$N-erc.json $N.kicad_sch >/dev/null 2>&1 || true
kicad-cli pcb drc --refill-zones --save-board --schematic-parity --severity-all --format json -o /tmp/$N-drc.json $N.kicad_pcb >/dev/null 2>&1 || true
python3 - "$N" <<'PY'
import json, sys, collections
n = sys.argv[1]
for kind in ("erc", "drc"):
    d = json.load(open(f"/tmp/{n}-{kind}.json"))
    vs = d.get("violations", []) + [v for s in d.get("sheets", []) for v in s.get("violations", [])]
    vs += d.get("unconnected_items", []) + d.get("schematic_parity", [])
    c = collections.Counter((v["severity"], v["type"]) for v in vs)
    print(kind.upper(), dict(c))
    for v in vs:
        items = "; ".join(f'{i["description"][:60]} @({i["pos"]["x"]-40:.2f},{i["pos"]["y"]-40:.2f})' for i in v.get("items", []))
        print(f'  {v["severity"][:4]} {v["type"]}: {items}')
PY
