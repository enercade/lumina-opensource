# Build your own Lumina — step-by-step guide

This guide takes you from zero to a working Lumina device, even if you have never used ESP-IDF or ordered a PCB before. Read it once before starting.

**What Lumina is**: a small ambient device (ESP32-C6 + a 12-LED ring + buzzer + buttons) that you control over MQTT. See the [project README](../README.md) for the story behind it.

## What you need

| Item | Notes |
|---|---|
| The Lumina PCB, assembled | Fabrication files come from [`lumina-hw/`](../lumina-hw/) — see Step 1 |
| 3D-printed enclosure | Models in [`lumina-mech/`](../lumina-mech/) — publication in progress |
| USB-C cable | Data-capable (not charge-only), to program the board |
| A computer | Windows, macOS or Linux |
| A 2.4 GHz WiFi network | ⚠️ The ESP32-C6 does **not** support 5 GHz-only networks |
| (Optional) [Node.js](https://nodejs.org) 18+ | Only to run the control app |

No soldering iron is strictly required if you order the PCB pre-assembled.

## Step 1 — Get the PCB made

Everything you need is already in [`lumina-hw/`](../lumina-hw/):

1. **Study the design** (optional): open `lumina-hw/pdf/schematic-luminaA00.PDF` for the full schematics and `PDF3D-luminaA00.PDF` for an interactive 3D view.
2. Upload the contents of **`lumina-hw/fabrication/`** to a PCB service that offers assembly (e.g. JLCPCB, PCBWay, Aisler, Eurocircuits): the `gerber/` folder for fabrication, `BOM-luminaA00.xlsx` and `pick_and_place-luminaA00.csv` for assembly.
3. Order the board **with assembly** — the ESP32-C6 module and the WS2812 LEDs are surface-mount parts.

Key parts on the board: ESP32-C6 module (WiFi 6, 2.4 GHz), 12× WS2812 addressable LEDs, passive buzzer, two side buttons (SW1/SW2), one central button (CN1), USB-C for power and programming.

## Step 2 — Print the enclosure

The STL models and printing guidelines will be published in [`lumina-mech/`](../lumina-mech/). Until then the bare board is fully functional for development.

## Step 3 — Install the firmware

The firmware lives in [`lumina-fw/`](../lumina-fw/) and is built with Espressif's ESP-IDF.

### 3a. Install ESP-IDF (one-time, ~20 minutes)

Follow Espressif's official guide for **ESP-IDF v5.4** and select the **ESP32-C6** target:
<https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32c6/get-started/>

- **Windows**: use the ESP-IDF Windows installer (it sets up Python, Git and the toolchain for you).
- **macOS/Linux**: follow the shell instructions (`install.sh` + `export.sh`).

### 3b. Build and flash

Connect the board over USB, open an ESP-IDF terminal, then:

```sh
git clone https://github.com/enercade/lumina-opensource.git
cd lumina-opensource/lumina-fw
idf.py set-target esp32c6      # first time only
idf.py build
idf.py -p <PORT> flash         # <PORT> = COM5 on Windows, /dev/ttyACM0 on Linux, etc.
```

To find the port: Device Manager on Windows (look for "USB Serial Device"), `ls /dev/tty*` on Linux/macOS.

> ⚠️ **After flashing, press the reset button on the board** (or unplug/replug USB). Boards programmed through the built-in USB-Serial-JTAG port can stay in download mode after the flash tool's software reset.

The firmware contains **no WiFi credentials** — you configure them in the next step, on the device itself.

## Step 4 — Connect Lumina to your WiFi

On first boot the LED ring breathes **amber**: Lumina is in setup mode.

1. With your phone or laptop, join the open WiFi network **`lumina-setup-xxxxxx`**.
2. A setup page pops up automatically (captive portal). If it doesn't, browse to **`http://192.168.4.1`**.
3. Note the **Device ID** shown on the page (e.g. `lumina-a1b2c3d4e5f6`) — you will need it in the control app.
4. Tap **Scan networks**, pick your 2.4 GHz network, enter the password, **Save & connect**.
5. The device restarts: **blue breathing** while connecting, then a **light-blue halo** and a success chime when online.

**Alternative — WPS**: hold the AUX button (SW2) for 3 seconds (a beep confirms), release, then press the WPS button on your router within 2 minutes (violet breathing while pairing).

**Start over**: hold AUX (SW2) for 10+ seconds to erase the WiFi credentials and return to setup mode.

## Step 5 — Control it

Lumina talks MQTT through a broker — by default the free public HiveMQ broker, so there is nothing to install or register.

### With the control app

```sh
cd lumina-opensource/lumina-sw
npm install
npm run dev
```

Open the printed URL (usually `http://localhost:5173`), add your device by its **Device ID**, and control color, brightness, effects, buzzer and the central-button action. Full details in [`lumina-sw/README.md`](../lumina-sw/README.md).

### With any MQTT client

Publish JSON to `lumina-<id>/cmd` and watch retained state on `lumina-<id>/info`. Protocol reference in the [project README](../README.md#mqtt-protocol). Example with [mosquitto](https://mosquitto.org/):

```sh
mosquitto_pub -h broker.hivemq.com -t "lumina-a1b2c3d4e5f6/cmd" -m '{"color":"FF3300","effect":"breath"}'
mosquitto_sub -h broker.hivemq.com -t "lumina-a1b2c3d4e5f6/info"
```

> ⚠️ The public broker has **no access control**: anyone who knows your device ID can control your device. Fine for experimenting; for anything more, set a private broker URI in the setup portal (Advanced section).

## Quick reference

**Light states**

| Light | Meaning |
|---|---|
| Breathing amber | Setup mode — join `lumina-setup-xxxxxx` and configure WiFi |
| Breathing blue | Connecting to your WiFi |
| Breathing violet | WPS pairing window open |
| Light blue halo | Online and ready |

**Buttons**

| Gesture | Action |
|---|---|
| SW1 press | Brightness + |
| SW2 press | Brightness − |
| SW2 hold 3–10 s | WPS pairing (beep at 3 s) |
| SW2 hold ≥ 10 s | Forget WiFi, back to setup mode |
| CN1 press | Configurable: sparkle / buzzer / both |

## Troubleshooting

- **The setup network never appears** → the board may be stuck in download mode: press the reset button.
- **It won't join my network** → check the network is 2.4 GHz (5 GHz-only SSIDs are invisible to the ESP32-C6) and the password is correct; after 8 failed attempts Lumina returns to setup mode.
- **The app says offline** → device and app must use the **same broker**; check the device's `info` topic with any MQTT client.
- **Flashing fails** → try a lower baud rate (`idf.py -p <PORT> -b 115200 flash`) and a different USB cable/port.

---

*This guide is part of the Lumina documentation, released under [CC BY-SA 4.0](../LICENSE-CC-BY-SA-4.0). Lumina is a project by [Enercade srl](https://www.enercade.com) within the COACH exploration of the EU LAUDS Factories project.*
