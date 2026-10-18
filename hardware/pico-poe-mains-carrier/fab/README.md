# Manufacturing files

These files are generated from the KiCad project by `tools/fab.sh`. Re-run the script after any change to the board.

| File | Use |
|---|---|
| `pico-poe-mains-carrier-gerbers.zip` | Gerbers and Excellon drill files. Upload this to the PCB fab, e.g. JLCPCB: 2 layers, 1.6 mm, HASL lead-free. |
| `pico-poe-mains-carrier-bom.csv` | BOM in JLCPCB format with LCSC part numbers. The W5500-EVB-Pico-PoE module is not included. |
| `pico-poe-mains-carrier-cpl.csv` | Component placement list for JLCPCB assembly. Check the rotation of U2, U3 and U4 in the preview. |
| `pico-poe-mains-carrier-schematic.pdf` | Schematic as PDF. |

The board has internal cutouts (the window under the module and the slot under the optocoupler) and notches in the outline. All are on the Edge.Cuts layer in the Gerbers.

3D models of the assembled board are in `../3d/`, and images are in `../images/`.
