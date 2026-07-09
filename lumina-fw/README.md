# Lumina firmware

ESP32-C6 firmware (ESP-IDF 5.4) for the Lumina ambient device. The device joins the local WiFi network and is controlled over MQTT; see the [project README](../README.md) for the full picture and the MQTT protocol.

## Features

- **WiFi provisioning, no hardcoded credentials**: on first boot (or when the stored network is unreachable) the device opens an open setup hotspot `lumina-setup-<xxxxxx>` with a captive portal at `http://192.168.4.1` where the user picks a network, enters the password and optionally a custom MQTT broker.
- **WPS push-button pairing**: hold the AUX button (SW2) for 3 s, then press the WPS button on the router.
- **MQTT control** on `lumina-<mac>/cmd` and `lumina-<mac>/info` (retained state + last-will), HiveMQ public broker by default.
- **Ambient network feedback**: the LED ring shows the connection state (amber = setup, blue = connecting, violet = WPS, light-blue halo = ready).

## Structure

```
lumina-fw/
├── CMakeLists.txt
├── sdkconfig.defaults          # esp32c6 target, 1.5 MB app partition
├── components/                 # Hardware drivers (self-contained)
│   ├── hw_leds/                # WS2812 ring + effect engine
│   ├── hw_btns/                # Debounced buttons with long-press events
│   └── hw_buzz/                # Passive buzzer patterns (LEDC PWM)
└── main/
    ├── config.h                # Pins, timings, network defaults (no secrets)
    ├── main.cpp                # Init, button dispatch, main loop
    ├── app_control.*           # Application state: LED/buzzer commands, magia
    ├── net_wifi.*              # WiFi manager: station / setup hotspot / WPS
    ├── net_portal.*            # Captive setup portal (HTTP + DNS)
    ├── net_mqtt.*              # MQTT client (cmd/info topics)
    └── wifi_dev_credentials.example.h   # Optional dev-only credential seeding
```

## Build / flash

Set up the [ESP-IDF 5.4 environment](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32c6/get-started/), then:

```sh
cd lumina-fw
idf.py set-target esp32c6     # first time only
idf.py build
idf.py -p <PORT> flash monitor
```

The device ID (`lumina-<mac>`) is shown on the setup portal page and printed on the serial console at boot.

## Configuration

- **WiFi credentials** are stored in NVS at runtime — never in the source tree. For development you can copy `main/wifi_dev_credentials.example.h` to `main/wifi_dev_credentials.h` (git-ignored) to seed your network. Seeding happens once per NVS lifetime, so "forget WiFi" (AUX held ≥ 10 s) still drops the device into the setup hotspot; erase the NVS partition (`idf.py erase-flash` or `esptool erase_region 0x9000 0x6000`) to seed again.
- **MQTT broker** defaults to `mqtt://broker.hivemq.com:1883` (`MQTT_DEFAULT_BROKER_URI` in `config.h`) and can be changed per device from the setup portal (Advanced section). The value is stored in NVS.
- **Pins and timings** live in `main/config.h`.

## Buttons

| Control | Action |
|---|---|
| SW1 short press | Brightness + |
| SW2 (AUX) short press | Brightness − |
| SW2 (AUX) held 3–10 s | Start WPS (a beep confirms the 3 s threshold) |
| SW2 (AUX) held ≥ 10 s | Erase WiFi credentials, restart in setup mode |
| CN1 (central) | Configurable: magia sparkle / buzzer / both (`cnmode`) |

## Troubleshooting

- **Device stays in download mode after flashing** (serial shows `waiting for download`): boards flashed through the built-in USB-Serial-JTAG port may latch download mode on the software reset issued by esptool. Press the reset button (or unplug/replug USB) to boot the application.
- **Device cannot find your network**: the ESP32-C6 radio is **2.4 GHz only**. If your SSID is 5 GHz-only, enable the 2.4 GHz band (or a dedicated 2.4 GHz SSID) on the router.

## License

Apache License 2.0 — see [LICENSE](LICENSE).
Copyright © 2026 Enercade srl, Torino, Italy.
