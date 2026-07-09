# Lumina

**An open-source ambient device that makes autonomous energy systems visible and understandable.**

Lumina is a small ambient device that translates the behaviour of distributed energy systems into clear light, haptic and audio cues. It is developed by [Enercade srl](https://www.enercade.com) within **COACH**, an exploration of the EU-funded **LAUDS Factories** project.

This repository contains everything needed to build, program and control a Lumina device: hardware sources, firmware, control software and (soon) the mechanical parts.

## Project context

### LAUDS Factories

[LAUDS Factories](https://lauds.eu/) (Local Accessible Urban Digital and Sustainable Factories) is a Horizon Europe research and innovation action (grant agreement [No. 101135986](https://cordis.europa.eu/project/id/101135986)) inspired by the New European Bauhaus. It promotes open, circular and distributed manufacturing: small, versatile urban factories where customised products are co-created and produced locally, in small series, with a reduced CO₂ footprint.

### COACH

COACH explores how people can better understand and influence autonomous energy systems. Today, many decisions in distributed energy networks happen out of sight, and this often limits trust and participation. COACH responds by developing **Lumina**, a small ambient device that turns complex energy decisions into simple ambient signals, helping communities engage more directly with renewable energy and the transition toward shared, sustainable autonomy.

The work is carried out over six months at **Bauhaus-Universität Weimar** and brings together user research, explainable AI and sustainable hardware development. Interviews and co-design sessions identify how people interpret energy information and what they need to feel in control of automated decisions. These insights shape a communication language that lets Lumina convey the reasoning behind system choices in an intuitive way. The project concludes with an integrated system, open-source fabrication guidelines and a structured methodology that can support similar initiatives across Europe.

## Repository structure

| Folder | Content | License |
|---|---|---|
| [`lumina-hw/`](lumina-hw/) | Electronics design sources (Altium Designer): schematics, PCB, BOM | [CERN-OHL-W v2](lumina-hw/LICENSE) |
| [`lumina-fw/`](lumina-fw/) | ESP32-C6 firmware (ESP-IDF): WiFi provisioning, WPS, MQTT control | [Apache 2.0](lumina-fw/LICENSE) |
| [`lumina-sw/`](lumina-sw/) | Local control app (React + MQTT over WebSocket) | [Apache 2.0](lumina-sw/LICENSE) |
| [`lumina-mech/`](lumina-mech/) | 3D-printable mechanical parts (STL) — coming soon | [CERN-OHL-W v2](lumina-mech/LICENSE) |
| Documentation (READMEs, guides) | | [CC BY-SA 4.0](LICENSE-CC-BY-SA-4.0) |

## How Lumina works

The device is built around an **ESP32-C6** driving a 12-LED WS2812 ring, a passive buzzer and four buttons.

```
┌──────────────┐   WiFi    ┌──────────────────┐   MQTT (WebSocket)   ┌───────────────┐
│    Lumina    │ ────────► │  MQTT broker     │ ◄──────────────────  │  Control app  │
│  (ESP32-C6)  │           │ (HiveMQ public)  │                      │   (React)     │
└──────────────┘           └──────────────────┘                      └───────────────┘
```

### First-time setup

1. Power the device. With no WiFi configured, it opens a **setup hotspot** (`lumina-setup-xxxxxx`, breathing amber light).
2. Connect with a phone or laptop — a captive portal pops up (or browse to `http://192.168.4.1`).
3. Pick your WiFi network, enter the password, save. The device restarts and joins your network (light blue halo when ready).

Alternatively, hold the **AUX button (SW2) for 3 seconds** and press the WPS button on your router — no password needed.

### Physical controls

| Control | Action |
|---|---|
| SW1 short press | Brightness + |
| SW2 (AUX) short press | Brightness − |
| SW2 (AUX) held 3–10 s | Start WPS pairing (a beep confirms the threshold) |
| SW2 (AUX) held ≥ 10 s | Erase WiFi credentials and return to setup mode |
| CN1 (central) | Configurable action: "magia" sparkle, buzzer, or both |

### Ambient network feedback

| Light | Meaning |
|---|---|
| Breathing amber | Setup hotspot active, waiting for configuration |
| Breathing blue | Connecting to your WiFi network |
| Breathing violet | WPS pairing window open |
| Light blue halo | Connected and ready |

## MQTT protocol

Each device uses a topic root derived from its WiFi MAC address: `lumina-<mac>` (12 lowercase hex digits, e.g. `lumina-a1b2c3d4e5f6`). The device ID is printed on the serial console at boot.

| Topic | Direction | Content |
|---|---|---|
| `lumina-<mac>/cmd` | app → device | JSON commands |
| `lumina-<mac>/info` | device → app | Retained JSON state (also the `{"online":false}` last-will) |

Example command (all fields optional, combinable):

```json
{
  "power": 1,
  "color": "00AAFF",
  "brightness": 60,
  "effect": "halo",
  "buzzer": "success",
  "magia": true,
  "cnmode": "both",
  "get": "info"
}
```

Effects: `fix`, `breath`, `halo`, `bounce`, `demo`, `magia`. Buzzer patterns: `info`, `success`, `warning`, `error`. Central-button modes: `magia`, `buzzer`, `both`.

Example info message (retained):

```json
{
  "online": true, "device": "lumina", "fw": "2.0.0",
  "mac": "a1b2c3d4e5f6", "ip": "192.168.1.42", "ssid": "MyNetwork",
  "rssi": -54, "uptime_s": 3600, "broker": "mqtt://broker.hivemq.com:1883",
  "power": 1, "brightness": 60, "color": "00AAFF",
  "effect": "halo", "cnmode": "both"
}
```

> **Note on the public broker.** By default Lumina uses the free public HiveMQ broker (`broker.hivemq.com`), which requires no account but offers **no confidentiality or access control**: anyone who knows a device ID can observe and control it. This is fine for demos and development; for real deployments configure a private broker from the setup portal (Advanced → MQTT broker).

## Quick start

**Firmware** (requires [ESP-IDF 5.4](https://docs.espressif.com/projects/esp-idf/)):

```sh
cd lumina-fw
idf.py set-target esp32c6
idf.py build
idf.py -p <PORT> flash monitor
```

**Control app** (requires Node.js 18+):

```sh
cd lumina-sw
npm install
npm run dev
```

Open the printed URL, add your device by its ID and start controlling it. See the folder READMEs for details.

## Licensing

This project uses different licenses for different kinds of content, following common open-hardware practice:

- **Hardware & mechanics** ([`lumina-hw/`](lumina-hw/), [`lumina-mech/`](lumina-mech/)): [CERN Open Hardware Licence v2 — Weakly Reciprocal](https://cern-ohl.web.cern.ch/)
- **Firmware & software** ([`lumina-fw/`](lumina-fw/), [`lumina-sw/`](lumina-sw/)): [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
- **Documentation** (READMEs, guides): [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)

## About

Lumina is designed and developed by **Enercade srl**, Corso Galileo Ferraris 80, 10129 Torino, Italy — [info@enercade.com](mailto:info@enercade.com).

Developed within the COACH exploration of the LAUDS Factories project in collaboration with Bauhaus-Universität Weimar.

*LAUDS Factories is funded by the European Union under the Horizon Europe programme, grant agreement No. 101135986. Views and opinions expressed are those of the authors only and do not necessarily reflect those of the European Union. Neither the European Union nor the granting authority can be held responsible for them.*
