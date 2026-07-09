# Lumina hardware

Fabrication outputs for the Lumina board, revision **A00**. Everything needed to have the PCB manufactured and assembled is included; open the PDFs to study the design.

> **Note on sources**: the board is designed in Altium Designer. The editable design sources are not part of this repository — the published fabrication outputs below are the covered design, licensed under CERN-OHL-W v2.

## Contents

| Path | Content |
|---|---|
| `fabrication/gerber/` | Gerber X2 layer set + NC drill (plated and non-plated) + board profile |
| `fabrication/BOM-luminaA00.xlsx` | Bill of materials |
| `fabrication/pick_and_place-luminaA00.csv` | Component placement for assembly |
| `pdf/schematic-luminaA00.PDF` | Full schematics (human-readable) |
| `pdf/drawing-luminaA00.PDF` | Board drawing |
| `pdf/PDF3D-luminaA00.PDF` | Interactive 3D view of the board (open in Adobe Reader) |
| `step/STEP-luminaA00.step` | 3D model for mechanical integration |

## Ordering the board

Upload the contents of `fabrication/` to a PCB service that offers assembly (JLCPCB, PCBWay, Aisler, Eurocircuits, …): Gerbers for fabrication, BOM + pick-and-place for assembly. See the [build guide](../docs/BUILDING.md) for the full walkthrough.

## Key design points

- **MCU**: Espressif ESP32-C6 (WiFi 6 — 2.4 GHz, BLE, 802.15.4, RISC-V)
- **Lighting**: 12× WS2812 addressable LEDs with switchable power rail (GPIO20, active low)
- **UI**: 2 side buttons (SW1/SW2), central button (CN1), spare connector button (CN2), passive buzzer (GPIO23)
- **Power/programming**: USB-C with the ESP32-C6 built-in USB-Serial-JTAG
- Pin assignments are documented in [`../lumina-fw/main/config.h`](../lumina-fw/main/config.h)

## License

CERN Open Hardware Licence Version 2 — Weakly Reciprocal (CERN-OHL-W v2) — see [LICENSE](LICENSE).
Copyright © 2026 Enercade srl, Torino, Italy.
