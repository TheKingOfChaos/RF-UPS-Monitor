#!/bin/bash
# Render board images into ../images and export 3D models into ../3d.
# Needs kicad-cli 10 with the KiCad 3D models installed (or KICAD10_3DMODEL_DIR set).
set -e
P=$(cd "$(dirname "$0")/.." && pwd)
N=pico-poe-mains-carrier
cd "$P"
# without the model library kicad-cli silently renders bare pads; in WSL use the Windows install, e.g.
# KICAD10_3DMODEL_DIR=/mnt/c/Users/<user>/AppData/Local/Programs/KiCad/10.0/share/kicad/3dmodels
M=${KICAD10_3DMODEL_DIR:-/usr/share/kicad/3dmodels}
if [ ! -f "$M/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x20_P2.54mm_Vertical.step" ]; then
  echo "KiCad 3D models not found in $M: set KICAD10_3DMODEL_DIR" >&2
  exit 1
fi
mkdir -p images 3d

# Two variants in temp folders: the carrier on its own (no module model), and the
# module on headers instead of soldered flat: two 1x20 female headers on the carrier (8.5 mm)
# and two 1x20 male headers under the module (2.5 mm body), so the module sits 11.0 mm up
T=$(mktemp -d)
H=$(mktemp -d)
cp $N.kicad_pro $N.kicad_dru "$T/"
cp $N.kicad_pro $N.kicad_dru "$H/"
ln -s "$P/3d" "$H/3d"
python3 - "$N.kicad_pcb" "$T/$N.kicad_pcb" "$H/$N.kicad_pcb" <<'PY'
import re, sys
s = open(sys.argv[1]).read()
model = r'\n\t\t\(model "\$\{KIPRJMOD\}/3d/W5500-EVB-Pico-PoE[^"]*"[\s\S]*?\n\t\t\)'
open(sys.argv[2], "w").write(re.sub(model, "", s, count=1))

SOCKET_H, PIN_BODY_H, MODULE_T = 8.5, 2.5, 0.62
def header(path, z, rot):
    return (f'\n\t\t(model "${{KICAD10_3DMODEL_DIR}}/{path}"'
            f'\n\t\t\t(offset\n\t\t\t\t(xyz 75.63 {-row_y} {z})\n\t\t\t)\n\t\t\t(scale\n\t\t\t\t(xyz 1 1 1)\n\t\t\t)'
            f'\n\t\t\t(rotate\n\t\t\t\t(xyz {rot})\n\t\t\t)\n\t\t)')
headers = ""
for row_y in (1.61, 19.39):  # header rows; pin 1/40 end at x = 75.63, rows run towards -x
    headers += header("Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x20_P2.54mm_Vertical.step", 0, "0 0 90")
    # male header upside down under the module: body on the socket, pins down into it
    headers += header("Connector_PinHeader_2.54mm.3dshapes/PinHeader_1x20_P2.54mm_Vertical.step", SOCKET_H + PIN_BODY_H, "0 180 90")
def raise_module(m):
    return m.group(0).replace("(xyz 0 -21 0.62)", f"(xyz 0 -21 {SOCKET_H + PIN_BODY_H + MODULE_T})") + headers
open(sys.argv[3], "w").write(re.sub(model, raise_module, s, count=1))
PY

R="kicad-cli pcb render --quality high --use-board-stackup-colors --preset follow_pcb_editor --background opaque"
$R --side top --width 2000 --height 1400 -o images/top.png $N.kicad_pcb >/dev/null
$R --side bottom --width 2000 --height 1400 -o images/bottom.png $N.kicad_pcb >/dev/null
$R --side top --width 2000 --height 1400 --perspective --rotate "-35,0,25" --zoom 0.9 -o images/iso.png $N.kicad_pcb >/dev/null
$R --side top --width 2000 --height 1400 --perspective --rotate "-35,0,-155" --zoom 0.9 -o images/iso-back.png $N.kicad_pcb >/dev/null
$R --side top --width 2000 --height 1400 -o images/top-carrier-only.png "$T/$N.kicad_pcb" >/dev/null
$R --side top --width 2000 --height 1400 --perspective --rotate "-35,0,25" --zoom 0.9 -o images/iso-carrier-only.png "$T/$N.kicad_pcb" >/dev/null
$R --side top --width 2000 --height 1400 --perspective --rotate "-35,0,25" --zoom 0.9 -o images/iso-headers.png "$H/$N.kicad_pcb" >/dev/null
$R --side top --width 2000 --height 1400 --perspective --rotate "-35,0,-155" --zoom 0.9 -o images/iso-back-headers.png "$H/$N.kicad_pcb" >/dev/null
$R --side front --width 2000 --height 1000 -o images/side-flat.png $N.kicad_pcb >/dev/null
$R --side front --width 2000 --height 1000 -o images/side-headers.png "$H/$N.kicad_pcb" >/dev/null

kicad-cli pcb export step --subst-models --force -o 3d/$N.step $N.kicad_pcb >/dev/null
kicad-cli pcb export glb --subst-models --force -o 3d/$N.glb $N.kicad_pcb >/dev/null
kicad-cli pcb export step --subst-models --force -o 3d/$N-carrier-only.step "$T/$N.kicad_pcb" >/dev/null
kicad-cli pcb export step --subst-models --force -o 3d/$N-headers.step "$H/$N.kicad_pcb" >/dev/null
kicad-cli pcb export glb --subst-models --force -o 3d/$N-headers.glb "$H/$N.kicad_pcb" >/dev/null

rm -r "$T" "$H"
ls -la images 3d
