# Lumina hardware

Electronics design sources for the Lumina device (Rev C00), authored in **Altium Designer**.

## Contents

| File | Content |
|---|---|
| `lumina.PrjPcb` | Altium project |
| `lumina.SchDoc` | Top-level schematic |
| `lumina-MCU.SchDoc` | ESP32-C6 MCU section |
| `lumina-LED.SchDoc` | WS2812 LED ring (12 LEDs) |
| `lumina-PWR.SchDoc` | Power supply |
| `lumina.PcbDoc` | PCB layout |
| `lumina.BomDoc` | Bill of materials |

## Key design points

- **MCU**: Espressif ESP32-C6 (WiFi 6 / BLE / 802.15.4, RISC-V)
- **Lighting**: 12× WS2812 addressable LEDs with switchable power rail (GPIO20, active low)
- **UI**: 2 side buttons (SW1/SW2), central button (CN1), spare connector button (CN2), passive buzzer (GPIO23)
- Pin assignments are documented in [`../lumina-fw/main/config.h`](../lumina-fw/main/config.h)

## License

CERN Open Hardware Licence Version 2 — Weakly Reciprocal (CERN-OHL-W v2) — see [LICENSE](LICENSE).
Copyright © 2026 Enercade srl, Torino, Italy.
